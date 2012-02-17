/* nomarch 1.4 - extract old `.arc' archives.
 * Copyright (C) 2001-2006 Russell Marks. See main.c for license details.
 *
 * Modified for xmp by Claudio Matsuoka, Aug 2007
 * - add quirks for Digital Symphony LZW packing
 * - add wrapper to read data from stream
 *
 * Modified for xmp by Claudio Matsuoka, Feb 2012
 * - remove global data
 *
 * readlzw.c - read (RLE+)LZW-compressed files.
 *
 * This is based on zgv's GIF reader. The LZW stuff is much the same, but
 * figuring out the details of the rather bizarre encoding involved much
 * wall therapy. %-(
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "readrle.h"

#include "common.h"
#include "readlzw.h"

static unsigned char *data_in_point,*data_in_max;
static unsigned char *data_out_point,*data_out_max;

struct local_data {
  /* now this is for the string table.
   * the data->st_ptr array stores which pos to back reference to,
   *  each string is [...]+ end char, [...] is traced back through
   *  the 'pointer' (index really), then back through the next, etc.
   *  a 'null pointer' is = to UNUSED.
   * the data->st_chr array gives the end char for each.
   *  an unoccupied slot is = to UNUSED.
   */
  #define UNUSED (-1)
  #define REALMAXSTR 65536
  int st_ptr[REALMAXSTR],st_chr[REALMAXSTR],st_last;
  int st_ptr1st[REALMAXSTR];
  
  /* this is for the byte -> bits mangler:
   *  dc_bitbox holds the bits, dc_bitsleft is number of bits left in dc_bitbox,
   */
  int dc_bitbox,dc_bitsleft;
  
  int codeofs;
  int global_use_rle,oldver;
  uint32 quirk;
  
  int maxstr;
  
  int st_oldverhashlinks[4096];	/* only used for 12-bit types */
  
  int nomarch_input_size;	/* hack for xmp, will fix later */
};

/* prototypes */
void code_resync(int old, struct local_data *);
void inittable(int orgcsize, struct local_data *);
int addstring(int oldcode,int chr, struct local_data *);
int readcode(int *newcode,int numbits, struct local_data *);
void outputstring(int code, struct local_data *);
void outputchr(int chr, struct local_data *);
int findfirstchr(int code, struct local_data *);


static unsigned char *_convert_lzw_dynamic(unsigned char *data_in,
                                   int max_bits,int use_rle,
                                   unsigned long in_len,
                                   unsigned long orig_len, int q,
				   struct local_data *data)
{
unsigned char *data_out;
int csize,orgcsize;
int newcode,oldcode,k=0;
int first=1,noadd;

//printf("in_len = %d, orig_len = %d\n", in_len, orig_len);
data->quirk = q;
data->global_use_rle=use_rle;
data->maxstr=(1<<max_bits);

if((data_out=malloc(orig_len))==NULL)
  fprintf(stderr,"nomarch: out of memory!\n"),exit(1);

data_in_point=data_in; data_in_max=data_in+in_len;
data_out_point=data_out; data_out_max=data_out+orig_len;
data->dc_bitbox=data->dc_bitsleft=0;
data->codeofs=0;
outputrle(-1,NULL);	/* init RLE */

data->oldver=0;
csize=9;		/* initial code size */
if(max_bits==0)		/* special case for static 12-bit */
  data->oldver=1,csize=12,data->maxstr=4096;
orgcsize=csize;
inittable(orgcsize, data);

oldcode=newcode=0;
if(data->quirk & NOMARCH_QUIRK_SKIPMAX)
  data_in_point++;	/* skip type 8 max. code size, always 12 */

if(max_bits==16)
  data->maxstr=(1<<*data_in_point++);	  /* but compress-type *may* change it (!) */

data->nomarch_input_size = 0;

while(1)
  {
//printf("input_size = %d        csize = %d\n", data->nomarch_input_size, csize);
  if(!readcode(&newcode,csize,data)) {
//printf("readcode failed!\n");
    break;
}
//printf("newcode = %x\n", newcode);

  if (data->quirk & NOMARCH_QUIRK_END101) {
    if (newcode == 0x101 /* data_out_point>=data_out_max */) {
//printf("end\n");
      break;
    }
  }

  noadd=0;
  if(first)
    {
    k=newcode,first=0;
    if(data->oldver) noadd=1;
    }

  if(newcode==256 && !data->oldver)
    {
    /* this *doesn't* reset the table (!), merely reduces code size again.
     * (It makes new strings by treading on the old entries.)
     * This took fscking forever to work out... :-(
     */
    data->st_last=255;

    if (data->quirk & NOMARCH_QUIRK_START101)	/* CM: Digital symphony data->quirk */
      data->st_last++;
    
    /* XXX do we need a resync if there's a reset when *already* csize==9?
     * (er, assuming that can ever happen?)
     */
    code_resync(csize, data);
    csize=orgcsize;
    if(!readcode(&newcode,csize,data))
      break;
    }

  if((!data->oldver && newcode<=data->st_last) ||
     (data->oldver && data->st_chr[newcode]!=UNUSED))
    {
    outputstring(newcode, data);
    k=findfirstchr(newcode, data);
    }
  else
    {
    /* this is a bit of an assumption, but these ones don't seem to happen in
     * non-broken files, so...
     */
#if 0
    /* actually, don't bother, just let the CRC tell the story. */
    if(newcode>data->st_last+1)
      fprintf(stderr,"warning: bad LZW code\n");
#endif
/*    k=findfirstchr(oldcode);*/	/* don't think I actually need this */
    outputstring(oldcode, data);
    outputchr(k, data);
    }

  if(data->st_last!=data->maxstr-1)
    {
    if(!noadd)
      {
      if(!addstring(oldcode,k,data))
        {
        /* XXX I think this is meant to be non-fatal?
         * well, nothing for now, anyway...
         */
        }
      if(data->st_last!=data->maxstr-1 && data->st_last==((1<<csize)-1))
        {
        csize++;
        code_resync(csize-1, data);
        }
      }
    }
    
  oldcode=newcode;
  }

if (~data->quirk & NOMARCH_QUIRK_NOCHK) {
  /* junk it on error */
  if(data_in_point!=data_in_max) {
    free(data_out);
    return(NULL);
  }
}

return(data_out);
}

unsigned char *convert_lzw_dynamic(unsigned char *data_in,
                                   int max_bits,int use_rle,
                                   unsigned long in_len,
                                   unsigned long orig_len, int q)
{
	struct local_data data;

	return _convert_lzw_dynamic(data_in, max_bits, use_rle, in_len,
						orig_len, q, &data);
}

unsigned char *read_lzw_dynamic(FILE *f, uint8 *buf, int max_bits,int use_rle,
			unsigned long in_len, unsigned long orig_len, int q)
{
	uint8 *buf2, *b;
	int pos;
	int size;
	struct local_data data;

	if ((buf2 = malloc(in_len)) == NULL)
		perror("read_lzw_dynamic"), exit(1);
	pos = ftell(f);
	fread(buf2, 1, in_len, f);
	b = _convert_lzw_dynamic(buf2, max_bits, use_rle, in_len, orig_len, q, &data);
	memcpy(buf, b, orig_len);
	size = q & NOMARCH_QUIRK_ALIGN4 ? ALIGN4(data.nomarch_input_size) :
						data.nomarch_input_size;
	fseek(f, pos + size, SEEK_SET);
	free(b);
	free(buf2);

	return buf;
}

/* uggghhhh, this is agonisingly painful. It turns out that
 * the original program bunched up codes into groups of 8, so we have
 * to waste on average about 5 or 6 bytes when we increase code size.
 * (and ghod, was this ever a complete bastard to track down.)
 * mmm, nice, tell me again why this format is dead?
 */
void code_resync(int old, struct local_data *data)
{
int tmp;

if (data->quirk & NOMARCH_QUIRK_NOSYNC)
  return;

while(data->codeofs)
  if(!readcode(&tmp,old,data))
    break;
}


void inittable(int orgcsize, struct local_data *data)
{
int f;
int numcols=(1<<(orgcsize-1));

for(f=0;f<REALMAXSTR;f++)
  {
  data->st_chr[f]=UNUSED;
  data->st_ptr[f]=UNUSED;
  data->st_ptr1st[f]=UNUSED;
  }

for(f=0;f<4096;f++)
  data->st_oldverhashlinks[f]=UNUSED;


if(data->oldver)
  {
  data->st_last=-1;		/* since it's a counter, when static */
  for(f=0;f<256;f++)
    addstring(0xffff,f,data);
  }
else
  {
  for(f=0;f<numcols;f++)
    data->st_chr[f]=f;
  data->st_last=numcols-1;      /* last occupied slot */

  if (data->quirk & NOMARCH_QUIRK_START101)	/* CM: Digital symphony quirk */
    data->st_last++;
  }
}


/* required for finding true table index in ver 1.x files */
int oldver_getidx(int oldcode,int chr, struct local_data *data)
{
int lasthash,hashval;
int a,f;

/* in type 5/6 crunched files, we hash the code into the array. This
 * means we don't have a real data->st_last, but for compatibility with
 * the rest of the code we pretend it still means that. (data->st_last
 * has already been incremented by the time we get called.) In our
 * case it's only useful as a measure of how full the table is.
 *
 * the hash is a mid-square thing.
 */
a=(((oldcode+chr)|0x800)&0xffff);
hashval=(((a*a)>>6)&0xfff);

/* first, check link chain from there */
while(data->st_chr[hashval]!=UNUSED && data->st_oldverhashlinks[hashval]!=UNUSED)
  hashval=data->st_oldverhashlinks[hashval];

/* make sure we return early if possible to avoid adding link */
if(data->st_chr[hashval]==UNUSED)
  return(hashval);

lasthash=hashval;

/* slightly odd approach if it's not in that - first try skipping
 * 101 entries, then try them one-by-one. It should be impossible
 * for this to loop indefinitely, if the table isn't full. (And we
 * shouldn't have been called if it was full...)
 */
hashval+=101;
hashval&=0xfff;

if(data->st_chr[hashval]!=UNUSED)
  {
  for(f=0;f<data->maxstr;f++,hashval++,hashval&=0xfff)
    if(data->st_chr[hashval]==UNUSED)
      break;
  if(hashval==data->maxstr)
    return(-1);		/* table full, can't happen */
  }

/* add link to here from the end of the chain */
data->st_oldverhashlinks[lasthash]=hashval;

return(hashval);
}


/* add a string specified by oldstring + chr to string table */
int addstring(int oldcode,int chr, struct local_data *data)
{
int idx;
//printf("oldcode = %02x\n", oldcode);

data->st_last++;
if((data->st_last&data->maxstr))
  {
  data->st_last=data->maxstr-1;
  return(1);		/* not too clear if it should die or not... */
  }

idx=data->st_last;
//printf("addstring idx=%x, oldcode=%02x, chr=%02x\n", idx, oldcode, chr);

if(data->oldver)
  {
  /* old version finds index in a rather odd way. */
  if((idx=oldver_getidx(oldcode,chr,data))==-1)
    return(0);
  }

data->st_chr[idx]=chr;

/* XXX should I re-enable this? think it would be ok... */
#if 0
if(data->st_last==oldcode)
  return(0);			/* corrupt */
#endif
if(oldcode>=data->maxstr) return(1);
data->st_ptr[idx]=oldcode;

if(data->st_ptr[oldcode]==UNUSED)          /* if we're pointing to a root... */
  data->st_ptr1st[idx]=oldcode;            /* then that holds the first char */
else                                 /* otherwise... */
  data->st_ptr1st[idx]=data->st_ptr1st[oldcode]; /* use their pointer to first */

return(1);
}


/* read a code of bitlength numbits */
int readcode(int *newcode,int numbits, struct local_data *data)
{
int bitsfilled,got;

bitsfilled=got=0;
(*newcode)=0;

while(bitsfilled<numbits)
  {
  if(data->dc_bitsleft==0)        /* have we run out of bits? */
    {
    if(data_in_point>=data_in_max) {
//printf("data_in_point=%p >= data_in_max=%p\n", data_in_point, data_in_max);
      return(0);
    }
    data->dc_bitbox=*data_in_point++;
    data->dc_bitsleft=8;
    data->nomarch_input_size++;	/* hack for xmp/dsym */
    }
  if(data->dc_bitsleft<numbits-bitsfilled)
    got=data->dc_bitsleft;
  else
    got=numbits-bitsfilled;

  if(data->oldver)
    {
    data->dc_bitbox&=0xff;
    data->dc_bitbox<<=got;
    bitsfilled+=got;
    (*newcode)|=((data->dc_bitbox>>8)<<(numbits-bitsfilled));
    data->dc_bitsleft-=got;
    }
  else
    {
    (*newcode)|=((data->dc_bitbox&((1<<got)-1))<<bitsfilled);
    data->dc_bitbox>>=got;
    data->dc_bitsleft-=got;
    bitsfilled+=got;
    }
  }

if((*newcode)<0 || (*newcode)>data->maxstr-1) {
//printf("*newcode (= %d) < 0 || *newcode (= %d) > data->maxstr (= %d) - 1\n", newcode, newcode, data->maxstr);
  return(0);
}

/* yuck... see code_resync() for explanation */
data->codeofs++;
data->codeofs&=7;

return(1);
}


void outputstring(int code, struct local_data *data)
{
static int buf[REALMAXSTR];
int *ptr=buf;

while(data->st_ptr[code]!=UNUSED && ptr<buf+data->maxstr)
  {
  *ptr++=data->st_chr[code];
  code=data->st_ptr[code];
  }

outputchr(data->st_chr[code], data);
while(ptr>buf)
  outputchr(*--ptr, data);
}


static void rawoutput(int byte)
{
//static int i = 0;
if(data_out_point<data_out_max)
  *data_out_point++=byte;
//printf(" output = %02x <================ %06x\n", byte, i++);
}


void outputchr(int chr, struct local_data *data)
{
if(data->global_use_rle)
  outputrle(chr,rawoutput);
else
  rawoutput(chr);
}


int findfirstchr(int code, struct local_data *data)
{
if(data->st_ptr[code]!=UNUSED)   /* not first? then use brand new st_ptr1st! */
  code=data->st_ptr1st[code];    /* now with no artificial colouring */
return(data->st_chr[code]);
}
