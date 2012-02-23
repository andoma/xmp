package org.helllabs.android.xmp;


public class Xmp {
	public static final int XMP_CTL_LOOP = 1 << 3;
	
	public native void initContext();
	public native int init(int rate);
	public native int deinit();
	public native boolean testModule(String name);
	public native int loadModule(String name);
	public native int releaseModule();
	public native int startPlayer();
	public native int endPlayer();
	public native int playFrame();	
	public native int softmixer();
	public native short[] getBuffer(int size, short buffer[]);
	public native int nextOrd();
	public native int prevOrd();
	public native int setOrd(int n);
	public native int stopModule();
	public native int restartModule();
	public native int stopTimer();
	public native int restartTimer();
	public native int incGvol();
	public native int decGvol();
	public native int seek(long time);
	public native ModInfo getModInfo(String name);
	public native int time();
	public native int seek(int seconds);
	public native int getPlayTempo();
	public native int getPlayBpm();
	public native int getPlayPos();
	public native int getPlayPat();
	public native String getVersion();
	public native String getTitle();
	public native int getFormatCount();
	public native String[] getFormats();
	public native String[] getInstruments();
	public native void getChannelData(int[] volumes, int[] instruments, int[] keys);
	public native void optAmplify(int n);
	public native void optMix(int n);
	public native void optStereo(boolean b);
	public native void optInterpolation(boolean b);
	public native void optFilter(boolean b);
	public native boolean testFlag(int flag);
	public native void setFlag(int flag);
	public native void resetFlag(int flag);
	
	static {
		System.loadLibrary("xmp");
	}
}
