package com.gibox.tool;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONObject;
import java.lang.ref.WeakReference;

/** Activity-owned controls: no system overlay permission, and only the controls consume touches. */
final class GameToolsPanel {
    private static WeakReference<GameToolsPanel> current=new WeakReference<>(null);
    private final Activity activity;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private FrameLayout root;private LinearLayout panel,body;private TextView status,speedLabel;private Button fly;private Stick stick;private ScrollView scroll;
    private boolean closed,flying,expanded,up,down;private float x,z,speed=5;private long arm=SystemClock.uptimeMillis(),nextState;
    private final Runnable pulse=this::poll;
    private GameToolsPanel(Activity activity){this.activity=activity;}
    static void attach(Activity activity){GameToolsPanel p=current.get();if(p!=null&&p.activity==activity&&!p.closed)return;if(p!=null)p.close();p=new GameToolsPanel(activity);current=new WeakReference<>(p);p.handler.post(p.pulse);}
    static void pause(Activity activity){GameToolsPanel p=current.get();if(p!=null&&p.activity==activity)p.close();}
    private int dp(float value){return Math.round(value*activity.getResources().getDisplayMetrics().density);}
    private void send(){try{NativeBridge.gameToolsInput(flying,x,z,(up?1:0)-(down?1:0),speed,arm);}catch(UnsatisfiedLinkError ignored){}}
    private void stop(){flying=false;x=z=0;up=down=false;send();if(stick!=null){stick.knobX=stick.knobY=0;stick.pointer=-1;stick.invalidate();}if(fly!=null)fly.setText("穿行：关");}
    private void close(){closed=true;handler.removeCallbacks(pulse);stop();remove();}
    private void remove(){if(root!=null){Object parent=root.getParent();if(parent instanceof ViewGroup)((ViewGroup)parent).removeView(root);}root=null;panel=body=null;status=speedLabel=null;fly=null;stick=null;scroll=null;expanded=false;}
    private void poll(){if(closed||activity.isFinishing()){close();return;}long now=SystemClock.uptimeMillis();
        if(now>=nextState){nextState=now+500;try{JSONObject state=new JSONObject(NativeBridge.gameToolsState());boolean allowed=state.optBoolean("allowed",false);
            if(!allowed){if(root!=null){stop();remove();}}else{if(root==null)create();boolean supported=state.optBoolean("supported",false);fly.setEnabled(supported);status.setText(supported?state.optString("message","就绪"):"当前客户端尚未适配");if(flying&&state.optLong("blockedArm",0)==arm)stop();}
        }catch(UnsatisfiedLinkError ignored){}catch(Exception e){if(status!=null)status.setText("工具状态读取失败");}}
        if(flying)send();handler.postDelayed(pulse,flying?50:250);
    }
    private Button button(String label){Button b=new Button(activity);b.setText(label);b.setTextSize(13);return b;}
    private void create(){root=new FrameLayout(activity);root.setClipChildren(false);panel=new LinearLayout(activity);panel.setOrientation(LinearLayout.VERTICAL);panel.setPadding(dp(8),dp(4),dp(8),dp(8));
        GradientDrawable bg=new GradientDrawable();bg.setColor(0xee20252b);bg.setCornerRadius(dp(16));panel.setBackground(bg);
        Button title=button("穿行");panel.addView(title,new LinearLayout.LayoutParams(dp(164),dp(44)));
        body=new LinearLayout(activity);body.setOrientation(LinearLayout.VERTICAL);scroll=new ScrollView(activity);scroll.setVisibility(View.GONE);scroll.addView(body);panel.addView(scroll,new LinearLayout.LayoutParams(dp(164),Math.max(dp(120),Math.min(dp(420),activity.getResources().getDisplayMetrics().heightPixels-dp(76)))));
        fly=button("穿行：关");fly.setOnClickListener(v->{if(flying)stop();else{arm++;flying=true;fly.setText("穿行：开");send();nextState=0;handler.removeCallbacks(pulse);handler.post(pulse);}});body.addView(fly);
        stick=new Stick();body.addView(stick,new LinearLayout.LayoutParams(dp(164),dp(140)));
        LinearLayout vertical=new LinearLayout(activity);vertical.setOrientation(LinearLayout.HORIZONTAL);Button rise=button("上升"),fall=button("下降");hold(rise,true);hold(fall,false);vertical.addView(rise,new LinearLayout.LayoutParams(0,dp(44),1));vertical.addView(fall,new LinearLayout.LayoutParams(0,dp(44),1));body.addView(vertical);
        speedLabel=new TextView(activity);speedLabel.setTextColor(0xffffffff);speedLabel.setTextSize(12);speedLabel.setText("速度：5");body.addView(speedLabel);
        SeekBar slider=new SeekBar(activity);slider.setMax(19);slider.setProgress(4);slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar bar,int value,boolean user){speed=value+1;speedLabel.setText("速度："+(value+1));if(flying)send();}public void onStartTrackingTouch(SeekBar bar){}public void onStopTrackingTouch(SeekBar bar){}});body.addView(slider);
        status=new TextView(activity);status.setTextColor(0xffdddddd);status.setTextSize(11);body.addView(status);
        FrameLayout.LayoutParams position=new FrameLayout.LayoutParams(dp(180),ViewGroup.LayoutParams.WRAP_CONTENT,Gravity.RIGHT|Gravity.CENTER_VERTICAL);position.rightMargin=dp(8);root.addView(panel,position);activity.addContentView(root,new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,ViewGroup.LayoutParams.MATCH_PARENT));
        title.setOnClickListener(v->{expanded=!expanded;if(!expanded)stop();scroll.setVisibility(expanded?View.VISIBLE:View.GONE);panel.setY(0);});
        title.setOnTouchListener(new View.OnTouchListener(){float rawX,rawY,startX,startY;boolean dragged;public boolean onTouch(View view,MotionEvent event){switch(event.getActionMasked()){
            case MotionEvent.ACTION_DOWN:rawX=event.getRawX();rawY=event.getRawY();startX=panel.getX();startY=panel.getY();dragged=false;return true;
            case MotionEvent.ACTION_MOVE:float dx=event.getRawX()-rawX,dy=event.getRawY()-rawY;dragged|=Math.hypot(dx,dy)>dp(6);if(dragged){panel.setX(Math.max(0,Math.min(root.getWidth()-panel.getWidth(),startX+dx)));panel.setY(Math.max(0,Math.min(root.getHeight()-panel.getHeight(),startY+dy)));}return true;
            case MotionEvent.ACTION_UP:if(!dragged)view.performClick();return true;case MotionEvent.ACTION_CANCEL:return true;default:return false;}}});
    }
    private void hold(Button button,boolean rising){button.setOnTouchListener(new View.OnTouchListener(){int pointer=-1;public boolean onTouch(View v,MotionEvent event){int action=event.getActionMasked();if(action==MotionEvent.ACTION_DOWN){pointer=event.getPointerId(0);if(rising)up=true;else down=true;send();return true;}
        if(action==MotionEvent.ACTION_CANCEL||action==MotionEvent.ACTION_UP||(action==MotionEvent.ACTION_POINTER_UP&&event.getPointerId(event.getActionIndex())==pointer)){pointer=-1;if(rising)up=false;else down=false;send();if(action==MotionEvent.ACTION_UP)v.performClick();}return true;}});}
    private final class Stick extends View {
        private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);private int pointer=-1;private float knobX,knobY;
        Stick(){super(activity);setContentDescription("穿行方向摇杆");}
        @Override protected void onDraw(Canvas canvas){super.onDraw(canvas);float cx=getWidth()*.5f,cy=getHeight()*.5f,r=Math.min(cx,cy)*.8f;paint.setColor(0xff495462);canvas.drawCircle(cx,cy,r,paint);paint.setColor(flying?0xffb9d9f7:0xff808890);canvas.drawCircle(cx+knobX*r,cy+knobY*r,r*.32f,paint);}
        private void release(){pointer=-1;knobX=knobY=x=z=0;send();invalidate();}
        @Override public boolean onTouchEvent(MotionEvent event){if(getParent()!=null)getParent().requestDisallowInterceptTouchEvent(true);if(!flying){release();return true;}int action=event.getActionMasked();if(action==MotionEvent.ACTION_DOWN)pointer=event.getPointerId(0);
            if(action==MotionEvent.ACTION_CANCEL||action==MotionEvent.ACTION_UP||(action==MotionEvent.ACTION_POINTER_UP&&event.getPointerId(event.getActionIndex())==pointer)){release();if(action==MotionEvent.ACTION_UP)performClick();return true;}
            int index=event.findPointerIndex(pointer);if(index<0){release();return true;}float radius=Math.min(getWidth(),getHeight())*.4f,dx=(event.getX(index)-getWidth()*.5f)/Math.max(1,radius),dy=(event.getY(index)-getHeight()*.5f)/Math.max(1,radius);float norm=Math.max(1f,(float)Math.hypot(dx,dy));knobX=dx/norm;knobY=dy/norm;x=knobX;z=-knobY;send();invalidate();return true;
        }
        @Override public boolean performClick(){return super.performClick();}
    }
}
