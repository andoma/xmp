package org.helllabs.android.xmp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.RemoteException;
import android.preference.PreferenceManager;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
import android.view.Menu;
import android.view.MenuInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;
import android.widget.ViewFlipper;

public class Player extends Activity {
	static final int SETTINGS_REQUEST = 45;
	private ModInterface modPlayer;	/* actual mod player */
	private ImageButton playButton, stopButton, backButton, forwardButton;
	private ImageButton loopButton;
	private SeekBar seekBar;
	private Thread progressThread;
	boolean seeking = false;
	boolean shuffleMode = true;
	boolean loopListMode = false;
	boolean paused = false;
	boolean finishing = false;
	boolean showInfoLine, showElapsed;
	private final TextView[] infoName = new TextView[2];
	private final TextView[] infoType = new TextView[2];
	private TextView infoStatus;
	private TextView elapsedTime;
	private ViewFlipper titleFlipper;
	private int flipperPage;
	private String[] fileArray = null;
	private int start;
	private SharedPreferences prefs;
	private FrameLayout viewerLayout;
	//BitmapDrawable image;
	private final Handler handler = new Handler();
	private int latency;
	private int totalTime;
	private boolean screenOn;
	private Activity activity;
	private AlertDialog deleteDialog;
	private BroadcastReceiver screenReceiver;
	private Viewer viewer;
	private Viewer.Info[] info;
	private final int[] modVars = new int[10];
	private static final int frameRate = 25;
	private boolean stopUpdate;
	private int currentViewer = 0;
	private boolean canChangeViewer = false;
	private Display display;
	
	private ServiceConnection connection = new ServiceConnection() {
		public void onServiceConnected(ComponentName className, IBinder service) {
			modPlayer = ModInterface.Stub.asInterface(service);
			flipperPage = 0;

			synchronized (modPlayer) {
				try {
					modPlayer.registerCallback(playerCallback);
				} catch (RemoteException e) { }

				if (fileArray != null && fileArray.length > 0) {
					// Start new queue
					playNewMod(fileArray, start);
				} else {
					// Reconnect to existing service
					try {
						showNewMod(modPlayer.getFileName());

						if (modPlayer.isPaused()) {
							pause();
						} else {
							unpause();
						}
					} catch (RemoteException e) { }
				}
			}
		}

		public void onServiceDisconnected(ComponentName className) {
			stopUpdate = true;
			modPlayer = null;
		}
	};
	
    private PlayerCallback playerCallback = new PlayerCallback.Stub() {
    	
        public void newModCallback(String name, String[] instruments) {
        	synchronized (modPlayer) {
        		Log.i("Xmp Player", "Show module data");
        		showNewMod(name);
        		canChangeViewer = true;
        	}
        }
        
        public void endModCallback() {
        	synchronized (modPlayer) {
        		Log.i("Xmp Player", "End of module");
        		stopUpdate = true;
        		canChangeViewer = false;
        	}
        }
        
        public void endPlayCallback() {
       		Log.i("Xmp Player", "End progress thread");
       		stopUpdate = true;

			if (progressThread != null && progressThread.isAlive()) {
				try {
					progressThread.join();
				} catch (InterruptedException e) { }
			}
			finish();
        }
    };
      
    final Runnable updateInfoRunnable = new Runnable() {
    	int oldSpd = -1;
    	int oldBpm = -1;
    	int oldPos = -1;
    	int oldPat = -1;
    	int oldTime = -1;
    	int before = 0, now;
    	boolean oldShowElapsed;
    	final char[] c = new char[2];
    	StringBuffer s = new StringBuffer();
    	
        public void run() {
        	now = (before + (frameRate * latency / 1000) + 1) % frameRate;
  
			try {
				synchronized (modPlayer) {
					if (stopUpdate)
						return;

					modPlayer.getInfo(info[now].values);							
					info[now].time = modPlayer.time() / 1000;
					
					if (info[before].values[0] < 0) {
						throw new Exception();
					}
	
					if (info[before].values[5] != oldSpd || info[before].values[6] != oldBpm
							|| info[before].values[0] != oldPos || info[before].values[1] != oldPat)
					{
						// Ugly code to avoid expensive String.format()
						
						s.delete(0, s.length());
						
						s.append("Speed:");
						Util.to02X(c, info[before].values[5]);
						s.append(c);
						
						s.append(" BPM:");
						Util.to02X(c, info[before].values[6]);
						s.append(c);
						
						s.append(" Pos:");
						Util.to02X(c, info[before].values[0]);
						s.append(c);
						
						s.append(" Pat:");
						Util.to02X(c, info[before].values[1]);
						s.append(c);
						
						infoStatus.setText(s);
	
						oldSpd = info[before].values[5];
						oldBpm = info[before].values[6];
						oldPos = info[before].values[0];
						oldPat = info[before].values[1];
					}
					if (info[before].time != oldTime || showElapsed != oldShowElapsed) {
						int t = info[before].time;
						if (t < 0)
							t = 0;
						
						s.delete(0, s.length());
						
						if (showElapsed) {	
							Util.to2d(c, t / 60);
							s.append(c);
							s.append(':');
							Util.to02d(c, t % 60);
							s.append(c);
							
							elapsedTime.setText(s);
						} else {
							t = totalTime - t;
							
							s.append('-');
							Util.to2d(c, t / 60);
							s.append(c);
							s.append(':');
							Util.to02d(c, t % 60);
							s.append(c);
							
							elapsedTime.setText(s);
						}
	
						oldTime = info[before].time;
						oldShowElapsed = showElapsed;
					}
	
					modPlayer.getChannelData(info[now].volumes, info[now].finalvols, info[now].pans,
							info[now].instruments, info[now].keys, info[now].periods);

					synchronized(viewerLayout) {
						viewer.update(info[before]);
					}
				}
			} catch (Exception e) {
				
			} finally {
				before++;
				if (before >= frameRate)
					before = 0;
			}
        }
    };
    
	private class ProgressThread extends Thread {
		@Override
    	public void run() {
			final long frameTime = 1000000000 / frameRate;
			long lastTimer = System.nanoTime();
			long now;
					
    		int t = 0;
    		
    		do {
    			synchronized (modPlayer) {
    				if (stopUpdate)
    					break;
    				
	    			try {
						t = modPlayer.time() / 100;
					} catch (RemoteException e) { }
    			}
	    			
	    		if (!paused && screenOn) {
	    			if (!seeking && t >= 0) {
	    				seekBar.setProgress(t);
	    			}
					handler.post(updateInfoRunnable);
				}
    			  			
	    		try {
	    			while ((now = System.nanoTime()) - lastTimer < frameTime && !stopUpdate) {
	    				sleep(10);
	    			}
	    			lastTimer = now;
	    		} catch (InterruptedException e) { }
    		} while (t >= 0 && !stopUpdate);
    		
    		seekBar.setProgress(0);
    	}
    };

	public void pause() {
		paused = true;
		playButton.setImageResource(R.drawable.play);
	}
	
	public void unpause() {
		paused = false;
		playButton.setImageResource(R.drawable.pause);
	}
	
	@Override
	public void onConfigurationChanged(Configuration newConfig) {
		super.onConfigurationChanged(newConfig);
		if (viewer != null)
			viewer.setRotation(display.getOrientation());
	}
	
	@Override
	protected void onNewIntent(Intent intent) {
		boolean reconnect = false;
		
		Log.i("Xmp Player", "Start player interface");
		
		String path = null;
		if (intent.getData() != null) {
			path = intent.getData().getPath();
		}

		fileArray = null;
		
		if (path != null) {		// from intent filter
			fileArray = new String[1];
			fileArray[0] = path;
			shuffleMode = false;
			loopListMode = false;
			start = 0;
		} else {	
			Bundle extras = intent.getExtras();
			if (extras != null) {
				fileArray = extras.getStringArray("files");	
				shuffleMode = extras.getBoolean("shuffle");
				loopListMode = extras.getBoolean("loop");
				start = extras.getInt("start");
			} else {
				reconnect = true;
			}
		}
		
    	Intent service = new Intent(this, ModService.class);
    	if (!reconnect) {
    		Log.i("Xmp Player", "Start service");
    		startService(service);
    	}
    	if (!bindService(service, connection, 0)) {
    		Log.e("Xmp Player", "Can't bind to service");
    		finish();
    	}
	}
	
    void setFont(TextView name, String path, int res) {
        Typeface font = Typeface.createFromAsset(this.getAssets(), path); 
        name.setTypeface(font); 
    }

    void changeViewer() {
    	currentViewer++;
    	currentViewer %= 3;
    	
    	synchronized(viewerLayout) {
    		viewerLayout.removeAllViews();
    		switch (currentViewer) {
    		case 0:
    			viewer = new InstrumentViewer(activity);
    			break;
    		case 1:
    			viewer = new ChannelViewer(activity);
    			break;
    		case 2:
    			viewer = new PatternViewer(activity);
    			break;
    		}
    			
    		viewerLayout.addView(viewer);   		
    		viewer.setup(modPlayer, modVars);
    		viewer.setRotation(display.getOrientation());
    	}
    }
	
	@Override
	public void onCreate(Bundle icicle) {
		super.onCreate(icicle);
		setContentView(R.layout.player);
		
		activity = this;
		display = ((WindowManager)getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
		
		Log.i("Xmp Player", "Create player interface");
		
        // INITIALIZE RECEIVER by jwei512
		IntentFilter filter = new IntentFilter(Intent.ACTION_SCREEN_ON);
		filter.addAction(Intent.ACTION_SCREEN_OFF);
		screenReceiver = new ScreenReceiver();
		registerReceiver(screenReceiver, filter);
		
		screenOn = true;
		
		if (ModService.isLoaded) {
			canChangeViewer = true;
		}
		
		setResult(RESULT_OK);
		prefs = PreferenceManager.getDefaultSharedPreferences(this);

		showInfoLine = prefs.getBoolean(Settings.PREF_SHOW_INFO_LINE, true);
		showElapsed = true;
		
		latency = prefs.getInt(Settings.PREF_BUFFER_MS, 500);
		if (latency > 1000) {
			latency = 1000;
		}

		onNewIntent(getIntent());
    	
		infoName[0] = (TextView)findViewById(R.id.info_name_0);
		infoType[0] = (TextView)findViewById(R.id.info_type_0);
		infoName[1] = (TextView)findViewById(R.id.info_name_1);
		infoType[1] = (TextView)findViewById(R.id.info_type_1);
		//infoMod = (TextView)findViewById(R.id.info_mod);
		infoStatus = (TextView)findViewById(R.id.info_status);
		elapsedTime = (TextView)findViewById(R.id.elapsed_time);
		titleFlipper = (ViewFlipper)findViewById(R.id.title_flipper);
		viewerLayout = (FrameLayout)findViewById(R.id.viewer_layout);

		viewer = new InstrumentViewer(this);
		viewerLayout.addView(viewer);
		viewerLayout.setOnClickListener(new OnClickListener() {
			@Override
			public void onClick(View v) {
				synchronized (modPlayer) {
					if (canChangeViewer) {
						changeViewer();
					}
				}
			}
		});		
			
		if (prefs.getBoolean(Settings.PREF_KEEP_SCREEN_ON, false)) {
			titleFlipper.setKeepScreenOn(true);
		}
		
		titleFlipper.setInAnimation(this, R.anim.slide_in_right);
		titleFlipper.setOutAnimation(this, R.anim.slide_out_left);

        Typeface font = Typeface.createFromAsset(this.getAssets(), "fonts/Michroma.ttf");
        
        for (int i = 0; i < 2; i++) {
        	infoName[i].setTypeface(font);
        	infoName[i].setIncludeFontPadding(false);
        	infoType[i].setTypeface(font);
        	infoType[i].setTextSize(TypedValue.COMPLEX_UNIT_DIP, 12);
        }
		
		if (!showInfoLine) {
			infoStatus.setVisibility(LinearLayout.GONE);
			elapsedTime.setVisibility(LinearLayout.GONE);
		}
		
		playButton = (ImageButton)findViewById(R.id.play);
		stopButton = (ImageButton)findViewById(R.id.stop);
		backButton = (ImageButton)findViewById(R.id.back);
		forwardButton = (ImageButton)findViewById(R.id.forward);
		loopButton = (ImageButton)findViewById(R.id.loop);

		/*
		// Set background here because we want to keep aspect ratio
		image = new BitmapDrawable(BitmapFactory.decodeResource(getResources(),
												R.drawable.logo));
		image.setGravity(Gravity.CENTER);
		image.setAlpha(48);
		viewerLayout.setBackgroundDrawable(image.getCurrent()); */
		
		loopButton.setImageResource(R.drawable.loop_off);
		
		loopButton.setOnClickListener(new OnClickListener() {
			public void onClick(View v) {
				try {
					if (modPlayer.toggleLoop()) {
						loopButton.setImageResource(R.drawable.loop_on);
					} else {
						loopButton.setImageResource(R.drawable.loop_off);
					}
				} catch (RemoteException e) {

				}
			}
		});
		
		playButton.setOnClickListener(new OnClickListener() {
			public void onClick(View v) {
				//Debug.startMethodTracing("xmp");				
				if (modPlayer == null)
					return;
				
				synchronized (this) {
					try {
						modPlayer.pause();
					} catch (RemoteException e) {

					}
					
					if (paused) {
						unpause();
					} else {
						pause();
					}
				}
		    }
		});
		
		stopButton.setOnClickListener(new OnClickListener() {
			public void onClick(View v) {
				//Debug.stopMethodTracing();
				if (modPlayer == null)
					return;
				
				stopPlayingMod();
		    }
		});
		
		backButton.setOnClickListener(new OnClickListener() {
			public void onClick(View v) {
				if (modPlayer == null)
					return;
				
				try {
					if (modPlayer.time() > 3000) {
						modPlayer.seek(0);
					} else {
						stopUpdate = true;
						synchronized (modPlayer) {
							modPlayer.prevSong();
						}
					}
					unpause();
				} catch (RemoteException e) {

				}
			}
		});
		
		forwardButton.setOnClickListener(new OnClickListener() {
			public void onClick(View v) {				
				if (modPlayer == null)
					return;
				
				try {
					stopUpdate = true;
					synchronized (modPlayer) {
						modPlayer.nextSong();
					}
				} catch (RemoteException e) { }
				
				unpause();
		    }
		});
		
		elapsedTime.setOnClickListener(new OnClickListener() {
			public void onClick(View v) {
				showElapsed ^= true;
		    }
		});
		
		seekBar = (SeekBar)findViewById(R.id.seek);
		seekBar.setProgress(0);
		
		seekBar.setOnSeekBarChangeListener(new OnSeekBarChangeListener() {
			public void onProgressChanged(SeekBar s, int p, boolean b) {
				// do nothing
			}

			public void onStartTrackingTouch(SeekBar s) {
				seeking = true;
			}

			public void onStopTrackingTouch(SeekBar s) {
				if (modPlayer != null) {
					try {
						modPlayer.seek(s.getProgress() * 100);
					} catch (RemoteException e) { }
				}
				seeking = false;
			}
		});
	}

	
	@Override
	public void onDestroy() {
		super.onDestroy();
		
		if (deleteDialog != null)
			deleteDialog.cancel();
		
		if (modPlayer != null) {
			try {
				modPlayer.unregisterCallback(playerCallback);
			} catch (RemoteException e) { }
		}
		
		unregisterReceiver(screenReceiver);
		
		Log.i("Xmp Player", "Unbind service");
		unbindService(connection);	
	}
	
	/*
	 * Stop screen updates when screen is off
	 */
	@Override
	protected void onPause() {
		// Screen is about to turn off
		if (ScreenReceiver.wasScreenOn) {
			screenOn = false;
		} else {
			// Screen state not changed
		}
		super.onPause();
	}

	@Override
	protected void onResume() {
		screenOn = true;
		super.onResume();
	}

	void showNewMod(String fileName) {

		if (deleteDialog != null)
			deleteDialog.cancel();
		handler.post(showNewModRunnable);
	}
	
	final Runnable showNewModRunnable = new Runnable() {
		public void run() {
			
			synchronized (modPlayer) {
				try {
					modPlayer.getModVars(modVars);
				} catch (RemoteException e) { }
				
				String name, type;
				try {
					name = modPlayer.getModName();
					type = modPlayer.getModType();
				} catch (RemoteException e) {
					name = "";
					type = "";
				}
				int time = modVars[0];
				/*int len = vars[1];
				int pat = vars[2];
				int chn = vars[3];
				int ins = vars[4];
				int smp = vars[5];*/
				
				totalTime = time / 1000;
		       	seekBar.setProgress(0);
		       	seekBar.setMax(time / 100);
		        
		       	flipperPage = (flipperPage + 1) % 2;
	
				infoName[flipperPage].setText(name);
			    infoType[flipperPage].setText(type);
	
		       	titleFlipper.showNext();
	
		       	viewer.setup(modPlayer, modVars);
				viewer.setRotation(display.getOrientation());
		       	
		       	/*infoMod.setText(String.format("Channels: %d\n" +
		       			"Length: %d, Patterns: %d\n" +
		       			"Instruments: %d, Samples: %d\n" +
		       			"Estimated play time: %dmin%02ds",
		       			chn, len, pat, ins, smp,
		       			((time + 500) / 60000), ((time + 500) / 1000) % 60));*/
				
				info = new Viewer.Info[frameRate];
				for (int i = 0; i < frameRate; i++) {
					info[i] = viewer.new Info();
				}
				
				stopUpdate = false;
		       	if (progressThread == null || !progressThread.isAlive()) {
		       		progressThread = new ProgressThread();
		       		progressThread.start();
		       	}
			}
		}
	};
	
	void playNewMod(String[] files, int start) {      	 
       	try {
			modPlayer.play(files, start, shuffleMode, loopListMode);
		} catch (RemoteException e) { }
	}
	
	void stopPlayingMod() {
		stopUpdate = true;
		
		if (finishing)
			return;
		
		finishing = true;
		
		synchronized (modPlayer) {
			try {
				modPlayer.stop();
			} catch (RemoteException e1) { }
		}
		
		paused = false;

		if (progressThread != null && progressThread.isAlive()) {
			try {
				progressThread.join();
			} catch (InterruptedException e) { }
		}
	}
	
	private DialogInterface.OnClickListener deleteDialogClickListener = new DialogInterface.OnClickListener() {
		public void onClick(DialogInterface dialog, int which) {
			if (which == DialogInterface.BUTTON_POSITIVE) {
				try {
					if (modPlayer.deleteFile()) {
						Message.toast(activity, "File deleted");
						setResult(RESULT_FIRST_USER);
						modPlayer.nextSong();
					} else {
						Message.toast(activity, "Can\'t delete file");
					}
				} catch (RemoteException e) {
					Message.toast(activity, "Can\'t connect service");
				}
	        }
	    }
	};
		
	// Menu
	
	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		if (prefs.getBoolean(Settings.PREF_ENABLE_DELETE, false)) {
			MenuInflater inflater = getMenuInflater();
			inflater.inflate(R.menu.player_menu, menu);
		}
	    return true;
	}

	@Override
	public boolean onOptionsItemSelected(MenuItem item) {
		switch(item.getItemId()) {
		case R.id.menu_delete:
			Message.yesNoDialog(activity, "Delete", "Are you sure to delete this file?", deleteDialogClickListener);
			break;
		}
		return true;
	}	
}
