package org.helllabs.java.xmp;


public class Xmp {
	private long opaque;

	public static final int XMP_FORMAT_8BIT = 1 << 0;
	public static final int XMP_FORMAT_UNSIGNED = 1 << 1;
	public static final int XMP_FORMAT_MONO = 1 << 2;

	static {
		System.loadLibrary("xmp");
	}
}
