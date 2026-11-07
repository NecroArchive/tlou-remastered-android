package ro.cobrabm.fexdroid;

/** Physical screen anchors inside the usable viewport, independent of menu position. */
final class HudPlacement {
    static int[] position(int width,int height,int hudWidth,int hudHeight,int margin,String corner,float nx,float ny) {
        int maxX=Math.max(0,width-hudWidth),maxY=Math.max(0,height-hudHeight);
        int left=Math.min(margin,maxX),right=Math.max(left,maxX-margin);
        int top=Math.min(margin,maxY),bottom=Math.max(top,maxY-margin);
        int x=left,y=top;
        switch(corner) {
            case "1":x=right;break;
            case "2":y=bottom;break;
            case "3":x=right;y=bottom;break;
            case "4":x=maxX/2;break;
            case "5":x=maxX/2;y=maxY/2;break;
            case "6":x=maxX/2;y=bottom;break;
            case "7":y=maxY/2;break;
            case "8":x=right;y=maxY/2;break;
            case "-1":x=Math.round(nx*maxX);y=Math.round(ny*maxY);break;
            default:break;
        }
        return new int[]{Math.max(0,Math.min(maxX,x)),Math.max(0,Math.min(maxY,y))};
    }
}
