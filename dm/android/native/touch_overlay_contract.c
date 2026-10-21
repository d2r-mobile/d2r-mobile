#include "touch_overlay_contract.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int root_allowed(const char *name) {
    static const char *allowed[]={"data","ctrl_touch","touch_strings.tsv","kbd_glyphs","features.txt","pack.json","pack_index.tsv","dm_textbox_plain.tsv"};
    for(unsigned i=0;i<sizeof allowed/sizeof *allowed;i++)if(!strcmp(name,allowed[i]))return 1;
    return 0;
}
static int regular(const char *path, off_t *size, char *why, size_t n) {
    struct stat st; if(lstat(path,&st)<0){snprintf(why,n,"stat %s: %s",path,strerror(errno));return -1;}
    if(!S_ISREG(st.st_mode)){snprintf(why,n,"not a regular file: %s",path);return -1;}
    if(size)*size=st.st_size; return 1;
}
static int strings_ok(const char *path, char *why, size_t n) {
    FILE *f=fopen(path,"r"); if(!f){snprintf(why,n,"open %s: %s",path,strerror(errno));return 0;}
    char line[4096]; int lines=0; int ok=1;
    while(ok&&fgets(line,sizeof line,f)) {
        lines++; size_t len=strcspn(line,"\r\n"); line[len]=0;
        if(!line[0])continue;
        char *tab1=strchr(line,'\t'); if(!tab1){ok=0;break;} *tab1=0;
        char *tab2=strchr(tab1+1,'\t'); if(!tab2){ok=0;break;} *tab2=0;
        int hex=strlen(tab1+1)==8; for(int i=0;hex&&i<8;i++){char c=tab1[1+i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))hex=0;}
        // The original generator uses an empty replacement to hide hints.
        // The third field must exist, but its text may intentionally be empty.
        if(!line[0]||!hex||strchr(tab2+1,'\t'))ok=0;
    }
    fclose(f);
    if(!ok)snprintf(why,n,"touch_strings.tsv line %d is not key<TAB>8-hex<TAB>value",lines);
    else if(!lines)snprintf(why,n,"touch_strings.tsv is empty");
    return ok&&lines>0;
}

int dm_touch_overlay_contract(const char *dir, char *why, size_t n) {
    if(why&&n)why[0]=0;
    if(!dir||!dir[0]){if(why&&n)snprintf(why,n,"overlay path empty");return -1;}
    struct stat st; if(lstat(dir,&st)<0)return errno==ENOENT?0:-1;
    if(!S_ISDIR(st.st_mode)){snprintf(why,n,"overlay is not a directory: %s",dir);return -1;}
    DIR *d=opendir(dir);if(!d){snprintf(why,n,"open %s: %s",dir,strerror(errno));return -1;}
    int marker=0,strings=0,data_seen=0;struct dirent *e;int bad=0;
    while(!bad&&(e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        if(!root_allowed(e->d_name)){snprintf(why,n,"unsupported root entry %s",e->d_name);bad=1;break;}
        char path[1400];snprintf(path,sizeof path,"%s/%s",dir,e->d_name);
        if(!strcmp(e->d_name,"data")){if(lstat(path,&st)<0||!S_ISDIR(st.st_mode)){snprintf(why,n,"data is not a directory");bad=1;}else data_seen=1;continue;}
        off_t size=0;if(regular(path,&size,why,n)<0){bad=1;break;}
        if(!strcmp(e->d_name,"ctrl_touch"))marker=size>0;
        else if(!strcmp(e->d_name,"touch_strings.tsv")){strings=size>0&&strings_ok(path,why,n);}
    }
    closedir(d);if(bad)return -1;
    if(!marker){snprintf(why,n,"ctrl_touch missing or empty");return -1;}
    if(!strings){if(!why[0])snprintf(why,n,"touch_strings.tsv missing or empty");return -1;}
    if(!data_seen){snprintf(why,n,"data directory missing");return -1;}
    char data[1400];snprintf(data,sizeof data,"%s/data",dir);d=opendir(data);
    if(!d){snprintf(why,n,"open %s: %s",data,strerror(errno));return -1;}
    int config=0,archives=0;
    while(!bad&&(e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        char path[1600];snprintf(path,sizeof path,"%s/%s",data,e->d_name);off_t size=0;
        if(regular(path,&size,why,n)<0){bad=1;break;}
        if(!strcmp(e->d_name,".build.config"))config=size>0;
        else if(strlen(e->d_name)>=5&&!strcmp(e->d_name+strlen(e->d_name)-5,".data")&&size>0)archives++;
        else{snprintf(why,n,"unsupported data entry %s",e->d_name);bad=1;}
    }
    closedir(d);if(bad)return -1;
    if(!config){snprintf(why,n,"data/.build.config missing or empty");return -1;}
    if(archives!=1){snprintf(why,n,"expected one non-empty .data archive, found %d",archives);return -1;}
    return 1;
}

int dm_touch_overlay_apply(const char *dir, char *why, size_t n) {
    int state=dm_touch_overlay_contract(dir,why,n);
    if(state==1)setenv("DM_ROMFS_OVERLAY",dir,1);
    else unsetenv("DM_ROMFS_OVERLAY");
    return state;
}
