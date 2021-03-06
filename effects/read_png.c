//read PNG image and put pixel data to LEDS
//readpng <channel>,<FILE>,<BACKCOLOR>,<start>,<len>,<offset>,<OR AND XOR =>,<DELAY>,<flip even rows>
//offset = where to start in PNG file
//backcolor = color to use for transparent area, FF0000 = RED
//P = use the PNG backcolor (default)
//W = use the alpha data for the White leds in RGBW LED strips
//DELAY = delay ms between 2 reads of LEN pixels, default=0 if 0 only <len> bytes at <offset> will be read
void readpng(thread_context * context,char * args){
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	
	char value[MAX_VAL_LEN];
	int channel=0;
	char filename[MAX_VAL_LEN];
	unsigned int start=0, len=0, offset=0,flip_rows=0;
	int op=0;
	int backcolor=0;
    int backcolortype=0; //0 = use PNG backcolor, 1 = use given backcolor, 2 = no backcolor but use alpha for white leds
	int delay=0;
	int row_index=0;
	
	args = read_channel(args, & channel);
	if (is_valid_channel_number(channel)) len=ledstring.channel[channel].count;
	args = read_str(args, filename, sizeof(filename));
	args = read_str(args, value, sizeof(filename));
	if (strlen(value)>=6){
		if (is_valid_channel_number(channel)){
			read_color(value, & backcolor, ledstring.channel[channel].color_size);
			backcolortype=1;
		}
	}else if (strcmp(value, "W")==0){
		backcolortype=2;
	}	
	args = read_int(args, &start);
	args = read_int(args, &len);
	args = read_int(args, &offset);
	args = read_str(args, value, sizeof(value));
	if (strcmp(value, "OR")==0) op=1;
	else if (strcmp(value, "AND")==0) op=2;
	else if (strcmp(value, "XOR")==0) op=3;
	else if (strcmp(value, "NOT")==0) op=4;
	args = read_int(args, &delay);
	args = read_int(args, &flip_rows);
	
	if (is_valid_channel_number(channel)){
		FILE * infile;		/* source file */
		ulg image_width, image_height, image_rowbytes;
		int image_channels,rc;
		uch *image_data;
		uch bg_red=0, bg_green=0, bg_blue=0;

		if (start<0) start=0;
        if (start+len> ledstring.channel[channel].count) len = ledstring.channel[channel].count-start;
		
		if (debug) printf("readpng %d,%s,%d,%d,%d,%d,%d,%d\n", channel, filename, backcolor, start, len,offset,op, delay);
		
		if ((infile = fopen(filename, "rb")) == NULL) {
			fprintf(stderr, "Error: can't open %s\n", filename);
			return;
		}
		
		if ((rc = readpng_init(infile, &image_width, &image_height)) != 0) {
            switch (rc) {
                case 1:
                    fprintf(stderr, "[%s] is not a PNG file: incorrect signature.\n", filename);
                    break;
                case 2:
                    fprintf(stderr, "[%s] has bad IHDR (libpng longjmp).\n", filename);
                    break;
                case 4:
                    fprintf(stderr, "Read PNG insufficient memory.\n");
                    break;
                default:
                    fprintf(stderr, "Unknown readpng_init() error.\n");
                    break;
            }
            fclose(infile);
			return;
        }
		
		//get the background color (for transparency support)
		if (backcolortype==0){
			if (readpng_get_bgcolor(&bg_red, &bg_green, &bg_blue) > 1){
				readpng_cleanup(TRUE);
				fclose(infile);
				fprintf(stderr, "libpng error while checking for background color\n");
				return;
			}
		}else{
			bg_red = get_red(backcolor);
			bg_green = get_green(backcolor);
			bg_blue = get_blue(backcolor);
		}
		
		//read entire image data
		image_data = readpng_get_image(2.2, &image_channels, &image_rowbytes);
		
		if (image_data) {
			int row=0, led_idx=0, png_idx=0, i=0;
			uch r, g, b, a;
			uch *src;
			
			ws2811_led_t * leds = ledstring.channel[channel].leds;
		
			if (start>=ledstring.channel[channel].count) start=0;
			if ((start+len)>ledstring.channel[channel].count) len=ledstring.channel[channel].count-start;
			
			led_idx=start; //start at this led index
			//load all pixels
			for (row = 0;  row < image_height; row++) {
				src = image_data + row * image_rowbytes;
				
				for (i = image_width;  i > 0;  --i) {
					r = *src++;
					g = *src++;
					b = *src++;
					
					if (image_channels != 3){
						a = *src++;
						if (backcolortype!=2){
							r = alpha_component(r, bg_red,a);
							g = alpha_component(g, bg_green,a);
							b = alpha_component(b, bg_blue,a);
						}
					}					
					if (png_idx>=offset){
						if (debug) printf("led %d= r %d,g %d,b %d,a %d, PNG channels=%d, PNG idx=%d\n", led_idx, r, g, b, a,image_channels,png_idx);
						if (led_idx < start + len){
							int fill_color;
							if (backcolortype==2 && ledstring.channel[channel].color_size>3){
								fill_color=color_rgbw(r,g,b,a);
							}else{
								fill_color=color(r,g,b);
							}
							
							if (flip_rows){ //this will horizontaly flip the row
								if (row_index & 1){
									led_idx = start + image_width * row_index + (image_width - i);
								}
							}
		
							switch (op){
								case 0:
									leds[led_idx].color=fill_color;
									break;
								case 1:
									leds[i].color|=fill_color;
									break;
								case 2:
									leds[i].color&=fill_color;
									break;
								case 3:
									leds[i].color^=fill_color;
									break;
								case 4:
									leds[i].color=~fill_color;
									break;
							}
							
						}
						led_idx++;
						if ( led_idx>=start + len){ 
							if (delay!=0){//reset led index if we are at end of led string and delay
								led_idx=start;
								row_index=0;
								ws2811_render(&ledstring);
								usleep(delay * 1000);
							}else{
								row = image_height; //exit reading
								i=0;
								break;
							}
						}
					}
					png_idx++;
					if (context->end_current_command) break; //signal to exit this command
				}
				if (context->end_current_command) break;
				row_index++;
			}
			readpng_cleanup(TRUE);
		}else{
			readpng_cleanup(FALSE);
			fprintf(stderr, "Unable to decode PNG image\n");
		}
		fclose(infile);
    }
}