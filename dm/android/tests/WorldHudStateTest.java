package org.d2r.android.port;

public final class WorldHudStateTest {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) {
        String[] labels = {"Rift","Level %d","Done %d","Late %d","Guardian","Next %d","Top",
            "Overtime","Wave %d/%d (%d/%d)","Slay %d/%d","Overtime slay","Overtime %d/%d","Same","Slay","Goal %d%%"};
        float[] h = new float[80];
        h[1]=100; h[2]=.75f;
        check(WorldHudState.title(h,labels).equals("Level 100"));
        check(WorldHudState.goal(h,labels).equals("Goal 75%"));
        h[4]=1; h[6]=1; h[7]=100;
        check(WorldHudState.title(h,labels).equals("Done 100"));
        check(WorldHudState.goal(h,labels).equals("Top"));
        h[1]=99; check(WorldHudState.goal(h,labels).equals("Next 100"));
        h[6]=0; check(WorldHudState.goal(h,labels).equals("Same"));
        check(WorldHudState.title(h,labels).equals("Late 99"));
        h[4]=0; h[5]=1; h[9]=5; h[10]=1; h[11]=2;
        check(WorldHudState.title(h,labels).equals("Guardian"));
        check(WorldHudState.goal(h,labels).equals("Overtime 1/2"));
        h[9]=3; check(WorldHudState.goal(h,labels).equals("Overtime"));
        h[9]=4; check(WorldHudState.goal(h,labels).equals("Overtime slay"));
        h[9]=1; check(WorldHudState.goal(h,labels).equals("Slay"));
        h[13]=3; h[12]=2; check(WorldHudState.goal(h,labels).equals("Wave 2/3 (1/2)"));
        check(WorldHudState.labelSuffix(1,true).equals("_old"));
        check(WorldHudState.labelSuffix(2,false).equals("_es"));
        check(WorldHudState.labelSuffix(3,false).equals("_mx"));
        check(WorldHudState.labelSuffix(4,false).equals("_ru"));
        check(WorldHudState.labelSuffix(0,false).equals("_en"));
        float[] v = new float[96], point = new float[5];
        v[0]=320; v[2]=2560; v[3]=1440;
        h[16]=2; h[17]=1; h[18]=1280; h[25]=1; h[28]=.5f; h[29]=.5f; h[32]=10;
        check(WorldHudState.project(v,h,0,point));
        check(Math.abs(point[0]-1632)<.01f && Math.abs(point[1]-736)<.01f && point[2]==1);
        for (float aspect : new float[]{4f/3,16f/9,20f/9,21f/9}) {
            v[0]=0; v[2]=720*aspect; v[3]=720;
            h[32]=10000; h[33]=100;
            check(WorldHudState.project(v,h,0,point) && point[2]==0);
            check(point[0]>=point[3] && point[0]<=point[4] && point[1]>=0 && point[1]<=720);
            if (point[0] > v[2]*.72f) check(point[1] <= 720*.45f);
        }
        v[0]=320; v[2]=2560; v[3]=1440;
        h[16]=1; h[20]=1; h[21]=.8f; h[22]=.05f; h[23]=.98f; h[24]=.3f; h[32]=h[33]=0;
        check(WorldHudState.project(v,h,0,point));
        check(Math.abs(point[0]-2598.4f)<.1f && Math.abs(point[1]-252)<.1f);
        v[1]=120; v[3]=1200;
        check(WorldHudState.project(v,h,0,point) && Math.abs(point[1]-330)<.1f);
        h[20]=0; check(!WorldHudState.project(v,h,0,point));
        h[16]=0; check(!WorldHudState.project(v,h,0,point));
        h[16]=2; h[17]=Float.NaN; check(!WorldHudState.project(v,h,0,point));
        check(!WorldHudState.project(v,h,12,point));
        System.out.println("PASS world HUD labels, completion/overtime, projection, viewport ratios and invalid states");
    }
}
