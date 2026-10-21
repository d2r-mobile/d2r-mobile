// 保留登记前缀的 AVL 区间树；只回答任意重叠，不回答最新版本或复制覆盖顺序。
// AVL interval trees retain registration prefixes; answer only any-overlap, never latest-version or copy-overlay ordering.
#include "pending_ranges.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { DM_PENDING_CHUNK_NODES=64 };
struct dm_pending_range_node {
    uintptr_t start,end,min_start,max_end;
    unsigned height;
    dm_pending_range_node *left,*right;
};
struct dm_pending_range_chunk {
    dm_pending_range_node nodes[DM_PENDING_CHUNK_NODES];
    size_t used;
    dm_pending_range_chunk *next;
};
struct dm_pending_range_version {
    uint64_t order;
    dm_pending_range_node *root;
};

static unsigned height(const dm_pending_range_node *node) {return node?node->height:0;}
static uintptr_t maximum(uintptr_t a,uintptr_t b) {return a>b?a:b;}
static dm_pending_range_node *node_new(dm_pending_ranges *ranges,uintptr_t start,uintptr_t end,dm_pending_range_node *left,dm_pending_range_node *right) {
    if(!ranges->current) {
        if(!ranges->chunks) {
            ranges->chunks=calloc(1,sizeof *ranges->chunks);
            if(!ranges->chunks){errno=ENOMEM;return NULL;}
            ranges->retained_bytes+=sizeof *ranges->chunks;
        }
        ranges->current=ranges->chunks;
    }
    while(ranges->current->used==DM_PENDING_CHUNK_NODES) {
        if(!ranges->current->next) {
            ranges->current->next=calloc(1,sizeof *ranges->current->next);
            if(!ranges->current->next){errno=ENOMEM;return NULL;}
            ranges->retained_bytes+=sizeof *ranges->current->next;
        }
        ranges->current=ranges->current->next;
    }
    dm_pending_range_node *node=&ranges->current->nodes[ranges->current->used++];
    *node=(dm_pending_range_node){.start=start,.end=end,.min_start=left?left->min_start:start,.max_end=end,.height=1,.left=left,.right=right};
    if(left)node->max_end=maximum(node->max_end,left->max_end);
    if(right)node->max_end=maximum(node->max_end,right->max_end);
    unsigned lh=height(left),rh=height(right);node->height=1+(lh>rh?lh:rh);
    ranges->live_nodes++;return node;
}

// 旋转产生新节点，不修改旧前缀使用的节点；未成功的插入不发布新根。
// Rotations create new nodes without changing nodes used by earlier prefixes; failed insertion never publishes its new root.
static dm_pending_range_node *rotate_right(dm_pending_ranges *ranges,const dm_pending_range_node *node) {
    const dm_pending_range_node *left=node->left;
    dm_pending_range_node *right=node_new(ranges,node->start,node->end,left->right,node->right);
    return right?node_new(ranges,left->start,left->end,left->left,right):NULL;
}
static dm_pending_range_node *rotate_left(dm_pending_ranges *ranges,const dm_pending_range_node *node) {
    const dm_pending_range_node *right=node->right;
    dm_pending_range_node *left=node_new(ranges,node->start,node->end,node->left,right->left);
    return left?node_new(ranges,right->start,right->end,left,right->right):NULL;
}
static dm_pending_range_node *insert_node(dm_pending_ranges *ranges,dm_pending_range_node *old,uintptr_t start,uintptr_t end) {
    if(!old)return node_new(ranges,start,end,NULL,NULL);
    if(start==old->start&&end==old->end)return old;
    dm_pending_range_node *left=old->left,*right=old->right;
    if(start<old->start||(start==old->start&&end<old->end)) {
        left=insert_node(ranges,left,start,end);if(!left)return NULL;
    }else {
        right=insert_node(ranges,right,start,end);if(!right)return NULL;
    }
    dm_pending_range_node *node=node_new(ranges,old->start,old->end,left,right);if(!node)return NULL;
    int balance=(int)height(left)-(int)height(right);
    if(balance>1) {
        if(height(left->left)<height(left->right)) {
            left=rotate_left(ranges,left);if(!left)return NULL;
            node=node_new(ranges,node->start,node->end,left,right);if(!node)return NULL;
        }
        return rotate_right(ranges,node);
    }
    if(balance< -1) {
        if(height(right->right)<height(right->left)) {
            right=rotate_right(ranges,right);if(!right)return NULL;
            node=node_new(ranges,node->start,node->end,left,right);if(!node)return NULL;
        }
        return rotate_left(ranges,node);
    }
    return node;
}

static int valid_range(const dm_pending_ranges *ranges,dm_pending_kind kind,uintptr_t address,uint64_t size) {
    if(!ranges||kind<DM_PENDING_UNKNOWN||kind>=DM_PENDING_KIND_COUNT){errno=EINVAL;return 0;}
    if(size>(uint64_t)(UINTPTR_MAX-address)){errno=EOVERFLOW;return 0;}
    return 1;
}
int dm_pending_ranges_insert(dm_pending_ranges *ranges,dm_pending_kind kind,uintptr_t address,uint64_t size,uint64_t order) {
    if(!valid_range(ranges,kind,address,size))return -1;
    if(order==DM_PENDING_ORDER_ALL){errno=EOVERFLOW;return -1;}
    if(!size)return 0;
    dm_pending_range_collection *collection=&ranges->collections[kind];
    if(collection->count&&order<=collection->versions[collection->count-1].order){errno=EINVAL;return -1;}
    if(collection->count==collection->capacity) {
        size_t next=collection->capacity?collection->capacity*2:16;
        if(next<collection->capacity||next>SIZE_MAX/sizeof *collection->versions){errno=EOVERFLOW;return -1;}
        dm_pending_range_version *versions=realloc(collection->versions,next*sizeof *versions);
        if(!versions){errno=ENOMEM;return -1;}
        ranges->retained_bytes+=(next-collection->capacity)*sizeof *versions;
        collection->versions=versions;collection->capacity=next;
    }
    dm_pending_range_node *old=collection->count?collection->versions[collection->count-1].root:NULL;
    dm_pending_range_node *root=insert_node(ranges,old,address,address+(uintptr_t)size);if(!root)return -1;
    collection->versions[collection->count++]=(dm_pending_range_version){order,root};return 1;
}

int dm_pending_ranges_overlap(const dm_pending_ranges *ranges,dm_pending_kind kind,uintptr_t address,uint64_t size,uint64_t before_order,size_t *visited) {
    if(visited)*visited=0;
    if(!valid_range(ranges,kind,address,size))return -1;
    if(!size||!before_order)return 0;
    const dm_pending_range_collection *collection=&ranges->collections[kind];
    // ALL 直接使用最新已发布根，避免每次普通预检重复二分前缀表。
    // ALL uses the latest published root directly, avoiding repeated prefix-table searches for ordinary preflight.
    size_t low=before_order==DM_PENDING_ORDER_ALL?collection->count:0,high=collection->count;
    while(low<high) {
        size_t middle=low+(high-low)/2;
        if(collection->versions[middle].order<before_order)low=middle+1;else high=middle;
    }
    if(!low)return 0;
    const dm_pending_range_node *node=collection->versions[low-1].root;uintptr_t end=address+(uintptr_t)size;
    while(node&&address<node->max_end&&end>node->min_start) {
        if(visited)(*visited)++;
        if(address<node->end&&node->start<end)return 1;
        // 若左子树末端超过起点，则存在可能命中；若其无命中，按起点排序的右子树也不可能命中。
        // If the left subtree ends after the query start it can match; if it does not, ordering by starts rules out a right-side match too.
        if(node->left&&node->left->max_end>address)node=node->left;else node=node->right;
    }
    return 0;
}

void dm_pending_ranges_reset(dm_pending_ranges *ranges) {
    if(!ranges)return;
    for(unsigned kind=0;kind<DM_PENDING_KIND_COUNT;kind++)ranges->collections[kind].count=0;
    for(dm_pending_range_chunk *chunk=ranges->chunks;chunk;chunk=chunk->next)chunk->used=0;
    ranges->current=ranges->chunks;ranges->live_nodes=0;
}
void dm_pending_ranges_destroy(dm_pending_ranges *ranges) {
    if(!ranges)return;
    for(unsigned kind=0;kind<DM_PENDING_KIND_COUNT;kind++)free(ranges->collections[kind].versions);
    for(dm_pending_range_chunk *chunk=ranges->chunks,*next;chunk;chunk=next){next=chunk->next;free(chunk);}
    memset(ranges,0,sizeof *ranges);
}
