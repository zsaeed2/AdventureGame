/*									tab:8
 *
 * photo.c - photo display functions
 *
 * "Copyright (c) 2011 by Steven S. Lumetta."
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * IN NO EVENT SHALL THE AUTHOR OR THE UNIVERSITY OF ILLINOIS BE LIABLE TO
 * ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
 * DAMAGES ARISING OUT  OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF THE AUTHOR AND/OR THE UNIVERSITY OF ILLINOIS HAS BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE AUTHOR AND THE UNIVERSITY OF ILLINOIS SPECIFICALLY DISCLAIM ANY
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE
 * PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND NEITHER THE AUTHOR NOR
 * THE UNIVERSITY OF ILLINOIS HAS ANY OBLIGATION TO PROVIDE MAINTENANCE,
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS."
 *
 * Author:	    Steve Lumetta
 * Version:	    3
 * Creation Date:   Fri Sep  9 21:44:10 2011
 * Filename:	    photo.c
 * History:
 *	SL	1	Fri Sep  9 21:44:10 2011
 *		First written (based on mazegame code).
 *	SL	2	Sun Sep 11 14:57:59 2011
 *		Completed initial implementation of functions.
 *	SL	3	Wed Sep 14 21:49:44 2011
 *		Cleaned up code for distribution.
 */


#include <string.h>

#include "assert.h"
#include "modex.h"
#include "photo.h"
#include "photo_headers.h"
#include "world.h"


/* types local to this file (declared in types.h) */

/*
 * A room photo.  Note that you must write the code that selects the
 * optimized palette colors and fills in the pixel data using them as
 * well as the code that sets up the VGA to make use of these colors.
 * Pixel data are stored as one-byte values starting from the upper
 * left and traversing the top row before returning to the left of
 * the second row, and so forth.  No padding should be used.
 */
struct photo_t {
    photo_header_t hdr;			/* defines height and width */
    uint8_t        palette[192][3];     /* optimized palette colors */
    uint8_t*       img;                 /* pixel data               */
};

struct octree_t {
  unsigned int color_count;
  unsigned int color_index;
  unsigned int pixel_index;
  unsigned int rgb[3];
};

/*
 * An object image.  The code for managing these images has been given
 * to you.  The data are simply loaded from a file, where they have
 * been stored as 2:2:2-bit RGB values (one byte each), including
 * transparent pixels (value OBJ_CLR_TRANSP).  As with the room photos,
 * pixel data are stored as one-byte values starting from the upper
 * left and traversing the top row before returning to the left of the
 * second row, and so forth.  No padding is used.
 */
struct image_t {
    photo_header_t hdr;			/* defines height and width */
    uint8_t*       img;                 /* pixel data               */
};


/* file-scope variables */
//level 2 and level four octree
struct octree_t levelTwo[LAYER_2];
struct octree_t levelFour[LAYER_4];
//write byte to specified port
#define OUTB(port,val)                                                  \
do {                                                                    \
    asm volatile ("                                                     \
        outb %b1,(%w0)                                                  \
    " : /* no outputs */                                                \
      : "d" ((port)), "a" ((val))                                       \
      : "memory", "cc");                                                \
} while (0)
//write values to two consecutive ports
#define REP_OUTSB(port,source,count)                                    \
do {                                                                    \
    asm volatile ("                                                     \
     1: movb 0(%1),%%al                                                ;\
	outb %%al,(%w2)                                                ;\
	incl %1                                                        ;\
	decl %0                                                        ;\
	jne 1b                                                          \
    " : /* no outputs */                                                \
      : "c" ((count)), "S" ((source)), "d" ((port))                     \
      : "eax", "memory", "cc");                                         \
} while (0)

/*
 * The room currently shown on the screen.  This value is not known to
 * the mode X code, but is needed when filling buffers in callbacks from
 * that code (fill_horiz_buffer/fill_vert_buffer).  The value is set
 * by calling prep_room.
 */
static const room_t* cur_room = NULL;


/*
 * fill_horiz_buffer
 *   DESCRIPTION: Given the (x,y) map pixel coordinate of the leftmost
 *                pixel of a line to be drawn on the screen, this routine
 *                produces an image of the line.  Each pixel on the line
 *                is represented as a single byte in the image.
 *
 *                Note that this routine draws both the room photo and
 *                the objects in the room.
 *
 *   INPUTS: (x,y) -- leftmost pixel of line to be drawn
 *   OUTPUTS: buf -- buffer holding image data for the line
 *   RETURN VALUE: none
 *   SIDE EFFECTS: none
 */
void
fill_horiz_buffer (int x, int y, unsigned char buf[SCROLL_X_DIM])
{
    int            idx;   /* loop index over pixels in the line          */
    object_t*      obj;   /* loop index over objects in the current room */
    int            imgx;  /* loop index over pixels in object image      */
    int            yoff;  /* y offset into object image                  */
    uint8_t        pixel; /* pixel from object image                     */
    const photo_t* view;  /* room photo                                  */
    int32_t        obj_x; /* object x position                           */
    int32_t        obj_y; /* object y position                           */
    const image_t* img;   /* object image                                */

    /* Get pointer to current photo of current room. */
    view = room_photo (cur_room);

    /* Loop over pixels in line. */
    for (idx = 0; idx < SCROLL_X_DIM; idx++) {
        buf[idx] = (0 <= x + idx && view->hdr.width > x + idx ?
		    view->img[view->hdr.width * y + x + idx] : 0);
    }

    /* Loop over objects in the current room. */
    for (obj = room_contents_iterate (cur_room); NULL != obj;
    	 obj = obj_next (obj)) {
	obj_x = obj_get_x (obj);
	obj_y = obj_get_y (obj);
	img = obj_image (obj);

        /* Is object outside of the line we're drawing? */
	if (y < obj_y || y >= obj_y + img->hdr.height ||
	    x + SCROLL_X_DIM <= obj_x || x >= obj_x + img->hdr.width) {
	    continue;
	}

	/* The y offset of drawing is fixed. */
	yoff = (y - obj_y) * img->hdr.width;

	/*
	 * The x offsets depend on whether the object starts to the left
	 * or to the right of the starting point for the line being drawn.
	 */
	if (x <= obj_x) {
	    idx = obj_x - x;
	    imgx = 0;
	} else {
	    idx = 0;
	    imgx = x - obj_x;
	}

	/* Copy the object's pixel data. */
	for (; SCROLL_X_DIM > idx && img->hdr.width > imgx; idx++, imgx++) {
	    pixel = img->img[yoff + imgx];

	    /* Don't copy transparent pixels. */
	    if (OBJ_CLR_TRANSP != pixel) {
		buf[idx] = pixel;
	    }
	}
    }
}


/*
 * fill_vert_buffer
 *   DESCRIPTION: Given the (x,y) map pixel coordinate of the top pixel of
 *                a vertical line to be drawn on the screen, this routine
 *                produces an image of the line.  Each pixel on the line
 *                is represented as a single byte in the image.
 *
 *                Note that this routine draws both the room photo and
 *                the objects in the room.
 *
 *   INPUTS: (x,y) -- top pixel of line to be drawn
 *   OUTPUTS: buf -- buffer holding image data for the line
 *   RETURN VALUE: none
 *   SIDE EFFECTS: none
 */
void
fill_vert_buffer (int x, int y, unsigned char buf[SCROLL_Y_DIM])
{
    int            idx;   /* loop index over pixels in the line          */
    object_t*      obj;   /* loop index over objects in the current room */
    int            imgy;  /* loop index over pixels in object image      */
    int            xoff;  /* x offset into object image                  */
    uint8_t        pixel; /* pixel from object image                     */
    const photo_t* view;  /* room photo                                  */
    int32_t        obj_x; /* object x position                           */
    int32_t        obj_y; /* object y position                           */
    const image_t* img;   /* object image                                */

    /* Get pointer to current photo of current room. */
    view = room_photo (cur_room);

    /* Loop over pixels in line. */
    for (idx = 0; idx < SCROLL_Y_DIM; idx++) {
        buf[idx] = (0 <= y + idx && view->hdr.height > y + idx ?
		    view->img[view->hdr.width * (y + idx) + x] : 0);
    }

    /* Loop over objects in the current room. */
    for (obj = room_contents_iterate (cur_room); NULL != obj;
    	 obj = obj_next (obj)) {
	obj_x = obj_get_x (obj);
	obj_y = obj_get_y (obj);
	img = obj_image (obj);

        /* Is object outside of the line we're drawing? */
	if (x < obj_x || x >= obj_x + img->hdr.width ||
	    y + SCROLL_Y_DIM <= obj_y || y >= obj_y + img->hdr.height) {
	    continue;
	}

	/* The x offset of drawing is fixed. */
	xoff = x - obj_x;

	/*
	 * The y offsets depend on whether the object starts below or
	 * above the starting point for the line being drawn.
	 */
	if (y <= obj_y) {
	    idx = obj_y - y;
	    imgy = 0;
	} else {
	    idx = 0;
	    imgy = y - obj_y;
	}

	/* Copy the object's pixel data. */
	for (; SCROLL_Y_DIM > idx && img->hdr.height > imgy; idx++, imgy++) {
	    pixel = img->img[xoff + img->hdr.width * imgy];

	    /* Don't copy transparent pixels. */
	    if (OBJ_CLR_TRANSP != pixel) {
		buf[idx] = pixel;
	    }
	}
    }
}


/*
 * image_height
 *   DESCRIPTION: Get height of object image in pixels.
 *   INPUTS: im -- object image pointer
 *   OUTPUTS: none
 *   RETURN VALUE: height of object image im in pixels
 *   SIDE EFFECTS: none
 */
uint32_t
image_height (const image_t* im)
{
    return im->hdr.height;
}


/*
 * image_width
 *   DESCRIPTION: Get width of object image in pixels.
 *   INPUTS: im -- object image pointer
 *   OUTPUTS: none
 *   RETURN VALUE: width of object image im in pixels
 *   SIDE EFFECTS: none
 */
uint32_t
image_width (const image_t* im)
{
    return im->hdr.width;
}

/*
 * photo_height
 *   DESCRIPTION: Get height of room photo in pixels.
 *   INPUTS: p -- room photo pointer
 *   OUTPUTS: none
 *   RETURN VALUE: height of room photo p in pixels
 *   SIDE EFFECTS: none
 */
uint32_t
photo_height (const photo_t* p)
{
    return p->hdr.height;
}


/*
 * photo_width
 *   DESCRIPTION: Get width of room photo in pixels.
 *   INPUTS: p -- room photo pointer
 *   OUTPUTS: none
 *   RETURN VALUE: width of room photo p in pixels
 *   SIDE EFFECTS: none
 */
uint32_t
photo_width (const photo_t* p)
{
    return p->hdr.width;
}


/*
 * prep_room
 *   DESCRIPTION: Prepare a new room for display.  You might want to set
 *                up the VGA palette registers according to the color
 *                palette that you chose for this room.
 *   INPUTS: r -- pointer to the new room
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: changes recorded cur_room for this file
 */
void
prep_room (const room_t* r)
{
    /* Record the current room. */
    cur_room = r;
    photo_t * cur_photo = room_photo(cur_room);
    /* 6-bit RGB (red, green, blue) values for first 64 colors */
    /* these are coded for 2 bits red, 2 bits green, 2 bits blue */
  static unsigned char palette_RGB[64][3] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0x15},
    {0x00, 0x00, 0x2A}, {0x00, 0x00, 0x3F},
    {0x00, 0x15, 0x00}, {0x00, 0x15, 0x15},
    {0x00, 0x15, 0x2A}, {0x00, 0x15, 0x3F},
    {0x00, 0x2A, 0x00}, {0x00, 0x2A, 0x15},
    {0x00, 0x2A, 0x2A}, {0x00, 0x2A, 0x3F},
    {0x00, 0x3F, 0x00}, {0x00, 0x3F, 0x15},
    {0x00, 0x3F, 0x2A}, {0x00, 0x3F, 0x3F},
    {0x15, 0x00, 0x00}, {0x15, 0x00, 0x15},
    {0x15, 0x00, 0x2A}, {0x15, 0x00, 0x3F},
    {0x15, 0x15, 0x00}, {0x15, 0x15, 0x15},
    {0x15, 0x15, 0x2A}, {0x15, 0x15, 0x3F},
    {0x15, 0x2A, 0x00}, {0x15, 0x2A, 0x15},
    {0x15, 0x2A, 0x2A}, {0x15, 0x2A, 0x3F},
    {0x15, 0x3F, 0x00}, {0x15, 0x3F, 0x15},
    {0x15, 0x3F, 0x2A}, {0x15, 0x3F, 0x3F},
    {0x2A, 0x00, 0x00}, {0x2A, 0x00, 0x15},
    {0x2A, 0x00, 0x2A}, {0x2A, 0x00, 0x3F},
    {0x2A, 0x15, 0x00}, {0x2A, 0x15, 0x15},
    {0x2A, 0x15, 0x2A}, {0x2A, 0x15, 0x3F},
    {0x2A, 0x2A, 0x00}, {0x2A, 0x2A, 0x15},
    {0x2A, 0x2A, 0x2A}, {0x2A, 0x2A, 0x3F},
    {0x2A, 0x3F, 0x00}, {0x2A, 0x3F, 0x15},
    {0x2A, 0x3F, 0x2A}, {0x2A, 0x3F, 0x3F},
    {0x3F, 0x00, 0x00}, {0x3F, 0x00, 0x15},
    {0x3F, 0x00, 0x2A}, {0x3F, 0x00, 0x3F},
    {0x3F, 0x15, 0x00}, {0x3F, 0x15, 0x15},
    {0x3F, 0x15, 0x2A}, {0x3F, 0x15, 0x3F},
    {0x3F, 0x2A, 0x00}, {0x3F, 0x2A, 0x15},
    {0x3F, 0x2A, 0x2A}, {0x3F, 0x2A, 0x3F},
    {0x3F, 0x3F, 0x00}, {0x3F, 0x3F, 0x15},
    {0x3F, 0x3F, 0x2A}, {0x3F, 0x3F, 0x3F}};
    int i,j;
    for(i = 0; i < 192; i++) {
      for(j = 0; j < 3; j++) {
        palette_RGB[i+64][j] = cur_photo->palette[i][j];
      }
    }
    /* Start writing at color 0. */
    OUTB (0x03C8, 0x00);

    /* Write all 256 colors from array. */
    REP_OUTSB (0x03C9, palette_RGB, 256 * 3);
}


/*
 * read_obj_image
 *   DESCRIPTION: Read size and pixel data in 2:2:2 RGB format from a
 *                photo file and create an image structure from it.
 *   INPUTS: fname -- file name for input
 *   OUTPUTS: none
 *   RETURN VALUE: pointer to newly allocated photo on success, or NULL
 *                 on failure
 *   SIDE EFFECTS: dynamically allocates memory for the image
 */
image_t*
read_obj_image (const char* fname)
{
    FILE*    in;		/* input file               */
    image_t* img = NULL;	/* image structure          */
    uint16_t x;			/* index over image columns */
    uint16_t y;			/* index over image rows    */
    uint8_t  pixel;		/* one pixel from the file  */


    /*
     * Open the file, allocate the structure, read the header, do some
     * sanity checks on it, and allocate space to hold the image pixels.
     * If anything fails, clean up as necessary and return NULL.
     */
    if (NULL == (in = fopen (fname, "r+b")) ||
	NULL == (img = malloc (sizeof (*img))) ||
	NULL != (img->img = NULL) || /* false clause for initialization */
	1 != fread (&img->hdr, sizeof (img->hdr), 1, in) ||
	MAX_OBJECT_WIDTH < img->hdr.width ||
	MAX_OBJECT_HEIGHT < img->hdr.height ||
	NULL == (img->img = malloc
		 (img->hdr.width * img->hdr.height * sizeof (img->img[0])))) {
	if (NULL != img) {
	    if (NULL != img->img) {
	        free (img->img);
	    }
	    free (img);
	}
	if (NULL != in) {
	    (void)fclose (in);
	}
	return NULL;
    }

    /*
     * Loop over rows from bottom to top.  Note that the file is stored
     * in this order, whereas in memory we store the data in the reverse
     * order (top to bottom).
     */
    for (y = img->hdr.height; y-- > 0; ) {

	/* Loop over columns from left to right. */
	for (x = 0; img->hdr.width > x; x++) {

	    /*
	     * Try to read one 8-bit pixel.  On failure, clean up and
	     * return NULL.
	     */
	    if (1 != fread (&pixel, sizeof (pixel), 1, in)) {
		free (img->img);
		free (img);
	        (void)fclose (in);
		return NULL;
	    }

	    /* Store the pixel in the image data. */
	    img->img[img->hdr.width * y + x] = pixel;
	}
    }

    /* All done.  Return success. */
    (void)fclose (in);
    return img;
}


/*
 * read_photo
 *   DESCRIPTION: Read size and pixel data in 5:6:5 RGB format from a
 *                photo file and create a photo structure from it.
 *                Code provided simply maps to 2:2:2 RGB.  You must
 *                replace this code with palette color selection, and
 *                must map the image pixels into the palette colors that
 *                you have defined.
 *   INPUTS: fname -- file name for input
 *   OUTPUTS: none
 *   RETURN VALUE: pointer to newly allocated photo on success, or NULL
 *                 on failure
 *   SIDE EFFECTS: dynamically allocates memory for the photo
 */
photo_t*
read_photo (const char* fname)
{
    FILE*    in;	/* input file               */
    photo_t* p = NULL;	/* photo structure          */
    uint16_t x;		/* index over image columns */
    uint16_t y;		/* index over image rows    */
    uint16_t pixel;	/* one pixel from the file  */
    uint16_t rgb_cur[3];
    int p_index;
    uint16_t index_l4, index_l2;
    int i; // loop counter
    /*
     * Open the file, allocate the structure, read the header, do some
     * sanity checks on it, and allocate space to hold the photo pixels.
     * If anything fails, clean up as necessary and return NULL.
     */
    if (NULL == (in = fopen (fname, "r+b")) ||
	NULL == (p = malloc (sizeof (*p))) ||
	NULL != (p->img = NULL) || /* false clause for initialization */
	1 != fread (&p->hdr, sizeof (p->hdr), 1, in) ||
	MAX_PHOTO_WIDTH < p->hdr.width ||
	MAX_PHOTO_HEIGHT < p->hdr.height ||
	NULL == (p->img = malloc
		 (p->hdr.width * p->hdr.height * sizeof (p->img[0])))) {
	if (NULL != p) {
	    if (NULL != p->img) {
	        free (p->img);
	    }
	    free (p);
	}
	if (NULL != in) {
	    (void)fclose (in);
	}
	return NULL;
    }

  initialize_octrees();
    /*
     * Loop over rows from bottom to top.  Note that the file is stored
     * in this order, whereas in memory we store the data in the reverse
     * order (top to bottom).
     */
    for (y = p->hdr.height; y-- > 0; ) {

	     /* Loop over columns from left to right. */
	      for (x = 0; p->hdr.width > x; x++) {

	    /*
	     * Try to read one 16-bit pixel.  On failure, clean up and
	     * return NULL.
	     */
	    if (1 != fread (&pixel, sizeof (pixel), 1, in)) {
		      free (p->img);
		      free (p);
	        (void)fclose (in);
		      return NULL;
	    }
	    /*
	     * 16-bit pixel is coded as 5:6:5 RGB (5 bits red, 6 bits green,
	     * and 6 bits blue).  We change to 2:2:2, which we've set for the
	     * game objects.  You need to use the other 192 palette colors
	     * to specialize the appearance of each photo.
	     *
	     * In this code, you need to calculate the p->palette values,
	     * which encode 6-bit RGB as arrays of three uint8_t's.  When
	     * the game puts up a photo, you should then change the palette
	     * to match the colors needed for that photo.
	     */
       //isolate red by getting rid of GB values (6+5), mask with 11111 (5 bits)
       //and shift, making it a 6 bit value
       rgb_cur[0] = ((((pixel >> 11) & 0x1F)) << 1);
       //isolate green by getting rid og B value (5 bits), mask with 111111 (6 bits)
       //No need to shift because it is already six bits
       rgb_cur[1] = ((pixel >> 5) & 0x3F);
       //no need to isolate blue since it's already at the end, mask with 11111 (5 bits)
       //and shift, making it a 6 bit value
       rgb_cur[2] = ((pixel & 0x1F) << 1);
       //get index of octree color
       index_l4 = getIndex(pixel, 4);
       index_l2 = getIndex(pixel, 2);
      for(i = 0; i < 3; i++) {
        //add values of rgb_cur to octree rgb values
        levelTwo[index_l2].rgb[i] += rgb_cur[i];
        levelFour[index_l4].rgb[i] += rgb_cur[i];
      }
      //increment counts for both octrees
      levelTwo[index_l2].color_count++;
      levelFour[index_l4].color_count++;
    }
  }
  //sort levelFour octree with regards to count
  qsort(levelFour, LAYER_4, sizeof(struct octree_t), &compare);
  //add 128 colors with highest count to palette
  for(i = 0; i < 128; i++) {
    //store LevelFour color values into palette
    storeInPalette(p, 4, i);
    //loop through levelTwo and LevelFour and subtract out any edges
    //intersections so that there is no overlap
    int j;
    int l2 = (((levelFour[i].color_index >> 10) & 0x3) << 4) +
         (((levelFour[i].color_index >> 6) & 0x3) << 2) +
         ((levelFour[i].color_index >> 2) & 0x3);
    for(j = 0; j < 3; j++) {
      levelTwo[l2].rgb[j] = levelTwo[l2].rgb[j] - levelFour[i].rgb[j];
    }
    levelTwo[l2].color_count = levelTwo[l2].color_count - levelFour[i].color_count;
    //save index value for later. will come in handy
    levelFour[i].pixel_index = i + 64;
  }
  //store L2 value in palette
  storeInPalette(p, 2, 0);

  //set file pointer to beginning of file, offset by 4 bytes for image dimensions
  fseek(in, 4, SEEK_SET);

  /*
   * Loop over rows from bottom to top.  Note that the file is stored
   * in this order, whereas in memory we store the data in the reverse
   * order (top to bottom).
   */
  for (y = p->hdr.height; y-- > 0; ) {

    /* Loop over columns from left to right. */
    for (x = 0; p->hdr.width > x; x++) {

    /*
     * Try to read one 16-bit pixel.  On failure, clean up and
     * return NULL.
     */
    if (1 != fread (&pixel, sizeof (pixel), 1, in)) {
        free (p->img);
        free (p);
        (void)fclose (in);
        return NULL;
    }

    index_l4 = getIndex(pixel, 4);
    p_index = -1;
    //test to see if desired color is in level four
    for(i = 0; i < 128; i++) {
      if(levelFour[i].color_index == index_l4) {
        p_index = levelFour[i].pixel_index;
        break;
      }
    }
    //if not then search level two
    if(p_index == -1) {
      p_index = getIndex(pixel, 2);
    }
    //drop palette down into image
    p->img[p->hdr.width * y + x] = 64 + p_index;
  }
}

    /* All done.  Return success. */
    (void)fclose (in);
    return p;
}

/*
* initialize_octrees
*   DESCRIPTION: Initialize level four and level two octrees
*   INPUTS: none
*   OUTPUTS: none
*   RETURN VALUE: none
*   SIDE EFFECTS: Set all octree values to 0
*/
void initialize_octrees() {
  int i,j;
  //initialize level two nodes
  for(i = 0; i < LAYER_2; i++) {
    for(j = 0; j < 3; j++) {
      levelTwo[i].rgb[j] = 0;
    }
    levelTwo[i].color_count = 0;
    levelTwo[i].color_index = i;
  }
  //initialize level four nodes
  for(i = 0; i < LAYER_4; i++) {
    for(j = 0; j < 3; j++) {
      levelFour[i].rgb[j] = 0;
    }
    levelFour[i].color_count = 0;
    levelFour[i].color_index = i;
  }
}

/*
* compare
*   DESCRIPTION: used by quicksort method to compare two values
*   INPUTS: left - left octree; right - right octree
*   OUTPUTS: none
*   RETURN VALUE:1 if left->cont < right->count, -1 if right->count < left->count, 0 otherwise
*   SIDE EFFECTS: none
*/
int compare(const void * left, const void * right) {
  const struct octree_t* l = (const struct octree_t*) left;
  const struct octree_t* r = (const struct octree_t*) right;
  //compares accordingly for compatibility with quicksort
  if(l->color_count < r->color_count)
    return 1;
  else if(l->color_count > r->color_count)
    return -1;
  else
    return 0;
}

/*
* getIndex
*   DESCRIPTION: get the index of pixel based on RGB value
*   INPUTS: pixel - pixel, val whether index of L2 or L4
*   OUTPUTS: none
*   RETURN VALUE:index of whatever layer specified
*   SIDE EFFECTS: none
*/
int getIndex(uint16_t pixel, int val) {
  int index = 0;
  int r, g, b;
  if(val == 2) {
    r = (pixel >> 11) & 0x1F; //Mask with  11111
	  g = (pixel >>  5) & 0x3F; //Mask with 111111
	  b = (pixel >>  0) & 0x1F; //Mask with  11111

	 index += ((r >> 3) << 4) + ((g >> 4) << 2) + ((b >> 3) << 0);
  }
  else if(val == 4) {
    //only use top 4 bits of pixel;
    r = ((pixel >> 11) & 0x1F) >> 1; //Mask with  11111
  	g = ((pixel >>  5) & 0x3F) >> 2; //Mask with 111111
  	b = ((pixel >>  0) & 0x1F) >> 1; //Mask with  11111

  	/* Put RGB in index as a concatination of bits: RRRRGGGGBBBB */
  	index += (r << 8); //Leave room for GB
  	index += (g << 4); //Leave room for B
  	index += b ;
  }
  return index;
}

/*
* storeInPalette
*   DESCRIPTION: Store color in palette
*   INPUTS: p - pointer to photo, val - level2 or level4, index - index at which to load color
*   OUTPUTS: none
*   RETURN VALUE:NULL
*   SIDE EFFECTS: fills palette
*/
void storeInPalette(photo_t* p, int val, int index) {
  int i,j; //loop counter
  if(val == 2) {//store L2 values
    for(i = 0; i < 64; i++) {
      if(levelTwo[i].color_count != 0) {
        for(j = 0; j < 3; j++) { //fill up palette according to algorithm
          p->palette[i][j] = levelTwo[i].rgb[j] / levelTwo[i].color_count;
        }
      }
      levelTwo[i].pixel_index = i;
    }
  }
  else if(val == 4) { //sore L4 values
    if(levelFour[index].color_count != 0) {
      for(i = 0; i < 3; i++) {//fill up palette according to algorithm
        p->palette[index+64][i] = levelFour[index].rgb[i] / levelFour[index].color_count;
      }
    }
  }
}
