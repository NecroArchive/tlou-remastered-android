package ro.cobrabm.fexdroid;
import android.graphics.Bitmap;
import android.util.Base64;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.zip.InflaterInputStream;
/** Alpha masks extracted from the approved TLOU action HUD reference. */
final class UnchartedActionGlyphs {
    private static final int SIZE=48;
    private static final String[][] DATA={
        {"l1","eNqNkrENwjAQRR0ihcJFxATpaRgh2YC0dPQUbEAyAkNAQ8EImBGyAYyQIkWEcD72+WKnQAhXT/bdv/9tC/H/ksfAeR+4en9npRmW57IaWUErrpnBrI61X4AuCaNDIcHl81akGJgbsQIcR7XIgcJPAmrGCHg0zDGwHjbcjUGOVRn65HJ13RVabxgYO61MN+HRvZX0o4wensxZiGKcpb5B6Zn3bApW3LwzkxLcLSaUQ9VORds662yhrHY8uHgnm4A0CcWcrt9djKQA7j0k+XGJ0jpwRmG3xIfJk94CRpjs75vJwenHn/gAq4t2fw=="},
        {"l2","eNpjYMADGA+Qxha7Cmdy/f//Asbe53ggfgKEyX6B8QDjDghbF6RXDsLeAmIzXQAxgSTIzA0gNrcDmK0BYstB7OIDsW0gbHYHILsEwmZuYABrArEZQQaBMEj+AZIbgfJMMDZQH1MDgs0MVMsIcqUJg2T0tFUpzF8YQ0N3hsqtfrVqC/sPplX7//0Dq2H+AtULMnsCmjkFSOZ/gLqBAeIGMHaAuqUAyc0WEDY3yDxZCBscKNwQtg2IDVQIYm8BB/gDRFgx1ILY8g4MkMACsqFhy/iF8YA0zHbpV69+wqMr+1UAIu4aGAiyGRqwxzUDxFIAyodLzQ=="},
        {"r1","eNqt0aFOxTAUBuC/bXKb7CSbhnCHQbJhZ0iKgScAjcaAQFxDUt6AR8BiyBUTc/QR+gg1eMRF3oyupxuahCPaL216enoK/F/cvS2U4xhmH3/e7Gc/AWeGKXaA9mwVZ5EPaBuHnl3h8BUbdo2PPRr2Wnzfm5rdKE+2RMG20hMqk2xEdEo3OdBKuegDaaNJulxGIH2RyxC+UiZbLhYgW2eTqUyTrVyLTbZ0A3oteP05yFBwfn1ltSvTvSAdCzvSJrtFR+kmKjBgW3Ij3uWX8PxKMWivbZc7R2aFbdItyqnU5H5yeiLi7nrqUoqHyefsR5wCJ+xu7maM9nK3mMaX33+8/sOf/wCrn0Es"},
        {"r2","eNpjYBjMQABCMYOI/f8hYD2Q////t1Wr9v//shvM/v0KyP4wkYGBHarkwyQGBu7//3+tWv3//5ffDAz6UPHfrxBqHjkwMDD9//8z/ALEDiD7KFMDnO3J5AC1+/9/V2aoc4B6r0VC2dz//6UlQNn8///CnSyPxNb//wfOtv//A86u//8Fzga6HM7+//8BjMn4//8FGJsJSZwha1XAYIohAKrcY8w="},
        {"triangle","eNqN0TESgjAQBdAEZ7RIQW3FDeQI0lnmSHAEb+IJnOUIHIHWSooUKRzWye6SpBBkq1dk/yQ/Sm1McU82LrOPPBt3WdxaB40YrMPMsObbkGynv8ZdHrf8TOcLbMB6tsYeHig5MAAubsfgN/k6BSO5zlx5MuWXM0Yb/O3Digs2daLZPV2f3XHNZH5WHTizy+AP+xQsP6ZjvCxL5aqK8bzciY+IL7Vzvn2uylo="},
        {"square","eNqtkbFKA0EQhveMSSC+gFxzb2AeIYeNhe1VCrGxs7jW7lLZWCUvkCKN3dVWm0dQRCwEc4WYIs0Jm2QVdn83O7ubtAEHFj6Ynf//mWHsX+t6iwcYBE5QBe7iyzeiIZ/Vjo/0VS4dF3jlngGJJQ1EgM7nlVMH3seCRg0vC+mcFlAQzqniGp9OUiQKH5Y5RGuEPCV5wS4VH5C8ZHyFlOQl695j86Vh+JjlylmJk1PST4C694IpSRqvb+2TPUNmPtkZ9AOlgTKvpjT4Ma0pudrauLYJrVOHY23Ypm+WeEOpbWB2g35xl9FyYnXLU7eo1uOv8juOJpBh4eeoA8d4Ctws0+21LnaOeLjHwf8AxHmmbw=="},
        {"circle","eNqdkjESATEUhhNmKLZwAIUj5AjiBgpG4RB7hHUTOobCBZAtdJocYQujoFEolmF/sUledozKq755k/f//0vC2F/VmAUWr8AJJDEwqXD6q88D8xFoNoaCwxow9foRcFiQEfZBBNpxE8GpVWFRYRXOf0YTTU6INckUIiXXXLh+B7h27sQ6cvEFzsOesjm7RuexVNIzsJI+AnCXbtbU0+tczuun98qYyEuum8MsuVFmmVwtt42xsoH6bIxM2aDFbo7c5uGl+o5WxGtrVyzWZpmN5VtD4WTlefbxywNH7in65V0cw/vywddHeAPqY6SU"},
        {"cross","eNqdkj0OwjAMhQMIRSIDDEww9AgcgSzsSIhzcAKULuwMnIJL0CPkCB26wMTgoVKhxm2cHyYkvORT8vzsOBHiv5hHHLSRh6gDjwJfsfGcIQVvG8K7z9sYLJ3d4UIn0LOqhUB89zwpSIVYcbLEdgHMU5LMmFcIoQODNjSJeNprX4wCEn55y29uascKsd2B7/+xleBLaaGejtc0BM+GPBRXvkHk7oIq53ZoVZrtac3YsrvrUodUMdZObpOx5oFlMnpVRc6iRBwjSkjkZbJfJC9ok1c+//wHH25tZQs="},
        {"l3","eNpjYCAfcP///xdE1///fwAn+wFMNT8Sm5tqbH4k89kmwNhcDWCKyQHhWP0fCPb/f3Am8///DkjsAzA2OxKbG4c4sl7G//8R5uf/RLBZAzDCjZ/GYYLC/rVqBYiltWo9SrzwI7FZQ0NDQGzR0FAHUtMAABE6XKg="},
        {"r3","eNqd0DEOgjAUBuBXNSGRQRZnFlfCFTiCN+AILg4OGthdPIKHcHDQ1CMQE8PKIdC4lN8Kj7Zsxk5fmr//ax7RrydwvHF8cbwdWCTW3s165tzHJr8YvK4tRWVmLykRZ/aJyH93nEZEq0PncJ8IxZHsXtCRS1CbSgG1s0baf2cMlDn/OgSelY+qrQHUurOOownE/JvxtZHzJE1lGgGe5Wk+uD7W7qdKgL/eRip2LE2hkCkavh6hNHGBF4q+JlPSrDJsrmZjHuz6Rjau5ybWkbPtCf17PlEmYIU="},
        {"shake","eNpjYMADGAMQbO4PSOwvBNiyELYAiF3vANZbAWLbX2BgB7L/gtj8H0Bsph8gNvMPkBp2iBV/QfbKHwCzbcEmQAxlgZqMF4iGhobC2P+BAEncAcbOWrVqFYy9H0kNkex/MLb+//8/4F7///8DEvvCUGaz///fAE8CiODECgDA2mWS"},
        {"pistol","eNpjYBjMwAJKyx9gYPgPA28YmODsPwzMQDL/1a79MPb/V6vAariR1Mgjsfnh7D0Q9f//rVq1kgFs5g/uB2DrGIEKFZgPgNlANR8UmBkw2OwgNdwQNsjM/Xvg7AehARC2/v//B2BesUdlO8DYQIfDvfsfwQY64R+MDbT2LxL7D4zNjcRGFqcnAAAYN4RL"},
        {"brick","eNq90b1qAkEQB/C5gB5kC+0kBLw38B7B08LaRgLJQ1xr5d4j+ByBKARSWNyqb2BhZ6Ngeg0T2Ssu48x9mSdwqx//2YWZWYC7n9o/vw4rtqLfko4BPS38NhuZRfEwVoTdvBBa9X35SoSPqZkHejUWz/eawD82Jf4bmJcPhU/sZepZd9vpxQAKPfNOa/RTAPfUOHm79IHYYEw9VuiLxwpdq/ok1tEi0Hs/NzbshOPM1COJCcXYrjMpElMQMi1kPkgsLfPVMzPrWOXOJnGW4iSf8FlcTF5jJ+VGwnMZc9c/VQzO5+a2zOb9/xKuAZuGSg=="},
        {"bottle","eNpjYCAPcCQg2BZ6CHYL0wE4m0VgAZwtIyEAZwu2ItSbHkCI54towpjs1xtYHaBs+Q8OcCX1f+BMxv9wNvf//x8YGCHs/f//P4CyGf///3+AgQPMZgaz4crhbH4g+wKUbY/E3o9gg4yBqWdGYoO0/oc6TR/EhrkRyIQ5DST8A0nrB4QLsBljj2TMf4QxTEhawcY8QHIlzCf6CK0M7P///0WEE8wFQHY9zBggiP6PiCrW/w2IOMknIR0AAMruepM="},
        {"medkit","eNpjYKANEFu1amkAlL1r1apVK6DsC0AsC2UvAGJdKHsDXLz+PwT8ALL3I7GRAOMqKFjJwMAMVfL/HwMDEzp7NYL9lxHB/sFAkP1vC8NuGPsL2CUQ9gewC/Gzf4YwiO5HmLmfFHv//wvD4RcomwHG/gtk50PZT4Bs1lAIcGCgJwAAP0rK3g=="}
    };
    private final Map<String,Bitmap> cache=new HashMap<>();
    private boolean decoded;
    Bitmap get(String name) {
        if(!decoded) {
            decoded=true;
            for(String[] entry:DATA)try {
                byte[] packed=new byte[SIZE*SIZE/4];
                try(InflaterInputStream stream=new InflaterInputStream(new ByteArrayInputStream(Base64.decode(entry[1],Base64.NO_WRAP)))) {
                    int offset=0;
                    while(offset<packed.length) {
                        int n=stream.read(packed,offset,packed.length-offset);
                        if(n<0)throw new IOException("short mask");
                        offset+=n;
                    }
                    if(stream.read()!=-1)throw new IOException("oversize mask");
                }
                int[] rgba=new int[SIZE*SIZE];
                for(int i=0;i<rgba.length;i++){
                    int a=((packed[i>>2]>>(6-2*(i&3)))&3)*85;
                    rgba[i]=(a<<24)|0x00ffffff;
                }
                Bitmap b=Bitmap.createBitmap(SIZE,SIZE,Bitmap.Config.ARGB_8888);
                b.setPixels(rgba,0,SIZE,0,0,SIZE,SIZE);
                cache.put(entry[0],b);
            } catch(IOException|RuntimeException e) {
                android.util.Log.e("TLOUTouch","Unable to decode "+entry[0],e);
            }
        }
        return cache.get(name);
    }
}
