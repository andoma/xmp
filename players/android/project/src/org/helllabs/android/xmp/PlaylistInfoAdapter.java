package org.helllabs.android.xmp;

import java.util.List;

import android.content.Context;
import android.graphics.Typeface;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.ImageView;
import android.widget.TextView;

public class PlaylistInfoAdapter extends ArrayAdapter<PlaylistInfo> {
    private List<PlaylistInfo> items;
    private Context myContext;
    private boolean useFilename;

    public PlaylistInfoAdapter(Context context, int resource, int textViewResourceId, List<PlaylistInfo> items, boolean useFilename) {
    	super(context, resource, textViewResourceId, items);
    	this.items = items;
    	this.myContext = context;
    	this.useFilename = useFilename;
    }
    
    @Override
    public View getView(int position, View convertView, ViewGroup parent) {
    	View v = convertView;
    	if (v == null) {
    		LayoutInflater vi = (LayoutInflater)myContext.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
    		v = vi.inflate(R.layout.playlist_item, null);
    	}
    	PlaylistInfo o = items.get(position);
    	           
    	if (o != null) {                		
    		TextView tt = (TextView)v.findViewById(R.id.plist_title);
    		TextView bt = (TextView)v.findViewById(R.id.plist_info);
    		ImageView im = (ImageView)v.findViewById(R.id.plist_image);
    		
   			tt.setText(useFilename ? FileUtils.basename(o.filename) : o.name);
   			bt.setText(o.comment);
   			
   			Typeface t = Typeface.create(Typeface.DEFAULT, Typeface.NORMAL);
   			if (o.imageRes == R.drawable.folder || o.imageRes == R.drawable.parent) {
   				tt.setTypeface(t, Typeface.ITALIC);
    			bt.setTypeface(t, Typeface.ITALIC);
   			} else {
   				tt.setTypeface(t, Typeface.NORMAL);
    			bt.setTypeface(t, Typeface.NORMAL);  			
   			}

   			if (o.imageRes > 0) {
   				im.setImageResource(o.imageRes);
   				im.setVisibility(View.VISIBLE);
   			} else {
   				im.setVisibility(View.GONE);
   			}
     	}
            
    	return v;
    }
}
