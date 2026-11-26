/* NODISKEMU - disk imager, device side (PET-directed X-IMG), see ximgdev.h */

#include <string.h>
#include "config.h"
#include "buffers.h"
#include "dirent.h"
#include "doscmd.h"
#include "errormsg.h"
#include "fatops.h"
#include "ff.h"
#include "parser.h"
#include "ustring.h"
#include "ximg.h"
#include "ximgdev.h"

uint8_t ximg_armed_dev;
bool    ximg_go;

static FIL      fh;
static bool     fh_open;
static uint8_t  mode;                   /* 'B' backup, 'R' restore */
static uint8_t  type;                   /* XIMG_D64/80/82, D60/D90 backup only */
static uint8_t  track;                  /* current track of the data phase */
static uint8_t  first, last;            /* armed track range */
static uint8_t  tbad[154];              /* per track: bad blocks, 0xff = failed */
static uint32_t etab;                   /* backup D64: error table offset, 0 = none */
static buffer_t *blk;

static uint8_t parse_num(uint8_t **p)
{
  uint8_t v = 0;
  while (**p == ' ')
    (*p)++;
  while (**p >= '0' && **p <= '9')
    v = (uint8_t)(v * 10 + (*(*p)++ - '0'));
  while (**p == ' ')                    /* BASIC prints numbers with spaces */
    (*p)++;
  if (**p == ',')
    (*p)++;
  while (**p == ' ')
    (*p)++;
  return v;
}

static void close_image(void)
{
  if (fh_open) {
    f_close(&fh);
    fh_open = false;
  }
  if (blk) {
    free_buffer(blk);
    blk = NULL;
  }
  ximg_armed_dev = 0;
  ximg_go = false;
}

static uint8_t tracks(void)
{
  /* D60/D90: track = cylinder (the drive counts from 0, we from 1) */
  return type == XIMG_D64 ? 35 : type == XIMG_D80 ? 77 :
         type == XIMG_D82 ? 154 : 153;
}

static uint16_t blocks(void)
{
  return type == XIMG_D64 ? 683 : type == XIMG_D80 ? 2083 : type == XIMG_D82 ? 4166 :
         (uint16_t)tracks() * ximg_sectors(type, 1);
}

static void open_image(uint8_t *p, uint8_t m)
{
  FRESULT res;
  bool replace = false;
  uint8_t t = parse_num(&p);

  close_image();
  if (t != 0 && t != XIMG_D64 && t != XIMG_D80 && t != XIMG_D82 &&
      !(m == 'B' && (t == XIMG_D60 || t == XIMG_D90))) {
    set_error(ERROR_SYNTAX_UNKNOWN);
    return;
  }
  if (!*p || (t == 0 && m == 'B')) {
    set_error(ERROR_SYNTAX_NONAME);
    return;
  }
  if (partition[current_part].fop != &fatops) {
    set_error(ERROR_IMAGE_INVALID);     /* only in a FAT directory */
    return;
  }
  blk = alloc_system_buffer();
  if (!blk) {
    set_error(ERROR_NO_CHANNEL);
    return;
  }
  /* @name or @:name: replace. FA_CREATE_ALWAYS frees the old clusters and
     points FatFs to that hole, so the new allocation finds them at once. */
  if (m == 'B' && *p == '@') {
    replace = true;
    p++;
    if (*p == ':')
      p++;
  }
  if (!*p) {
    free_buffer(blk);
    blk = NULL;
    set_error(ERROR_SYNTAX_NONAME);
    return;
  }
  ustrcpy(ops_scratch, p);
  pet2asc(ops_scratch);
  partition[current_part].fatfs->curr_dir = partition[current_part].current_dir.fat;
  res = f_open(partition[current_part].fatfs, &fh, ops_scratch,
               m == 'B' ? (FA_WRITE | FA_READ | (replace ? FA_CREATE_ALWAYS : FA_CREATE_NEW))
                        : (FA_READ | FA_OPEN_EXISTING));
  if (res != FR_OK) {
    free_buffer(blk);
    blk = NULL;
    set_error(res == FR_EXIST ? ERROR_FILE_EXISTS :
              res == FR_NO_FILE ? ERROR_FILE_NOT_FOUND : ERROR_DRIVE_NOT_READY);
    return;
  }
  if (m == 'R') {
    /* type 0: take it from the size (with or without error table) */
    uint8_t ts = ximg_type_from_size(fh.fsize);
    if (!ts || (t && ts != t) || ts == XIMG_D60 || ts == XIMG_D90) {
      f_close(&fh);
      free_buffer(blk);
      blk = NULL;
      set_error(ERROR_IMAGE_INVALID);
      return;
    }
    t = ts;
  }
  fh_open = true;
  mode = m;
  type = t;
  etab = 0;
  memset(tbad, 0, sizeof(tbad));
  if (m == 'B') {
    /* Allocate the whole image now: the cluster search of FatFs can take
       seconds on a big, filled card. Here the PET waits for the status
       anyway, during the data phase the floppy would wait for us. */
    uint32_t size = (uint32_t)blocks() * 256;
    if (f_lseek(&fh, size) != FR_OK || fh.fsize != size ||
        f_sync(&fh) != FR_OK || f_lseek(&fh, 0) != FR_OK) {
      f_close(&fh);
      f_unlink(partition[current_part].fatfs, ops_scratch);
      fh_open = false;
      free_buffer(blk);
      blk = NULL;
      set_error(ERROR_DISK_FULL);
      return;
    }
  }
  set_error_ts(ERROR_OK, t, 0);         /* track field: image type */
}

void ximg_command(void)
{
  uint8_t *p = command_buffer + 6;      /* after "X-IMG:" */

  command_buffer[command_length] = 0;
  if (command_length < 7 || memcmp(command_buffer, "X-IMG:", 6)) {
    set_error(ERROR_SYNTAX_UNKNOWN);
    return;
  }
  switch (*p) {
  case 'B':
  case 'R':
    open_image(p + 2, *p);
    break;

  case 'T':                             /* T,<unit>,<track> */
  case 'A':                             /* A,<unit>[,<first>[,<last>]] */
    {
      uint8_t c = *p, dev, f, l;
      p += 2;
      if (!fh_open) {
        set_error(ERROR_FILE_NOT_OPEN);
        return;
      }
      dev = parse_num(&p);
      f = parse_num(&p);
      l = parse_num(&p);
      if (c == 'T')
        l = f;
      if (!f)
        f = 1;
      if (!l)
        l = tracks();
      if (dev < 4 || dev > 30 || f > l || l > tracks()) {
        set_error(ERROR_SYNTAX_UNKNOWN);
        return;
      }
      first = f;
      last = l;
      ximg_go = false;
      ximg_armed_dev = dev;
    }
    break;

  case 'L':                             /* L[,<from>]: next track with errors */
    {
      uint8_t t;
      p += 2;
      t = parse_num(&p);
      if (!t)
        t = 1;
      for (; t <= tracks(); t++)
        if (tbad[t - 1])
          break;
      if (t > tracks())
        set_error_ts(ERROR_OK, 0, 0);
      else
        set_error_ts(ERROR_OK, t, tbad[t - 1]);
    }
    break;

  case 'E':
    /* every block was written, the file has its full size (error table
       included when there was one) */
    close_image();
    break;

  default:
    set_error(ERROR_SYNTAX_UNKNOWN);
    break;
  }
}

uint8_t ximg_dir(void)
{
  return mode == 'B' ? 'R' : 'W';
}

uint8_t ximg_first(void)
{
  return first;
}

uint8_t ximg_last(void)
{
  return last;
}

void ximg_set_track(uint8_t t)
{
  track = t;
}

void ximg_track_done(bool ok, uint8_t bad)
{
  tbad[track - 1] = ok ? bad : 0xff;
}

uint8_t ximg_nsec(void)
{
  return ximg_sectors(type, track);
}

uint8_t *ximg_block(void)
{
  return blk->data;
}

static bool img_write(uint32_t off, const uint8_t *d, UINT n)
{
  UINT w;
  return f_lseek(&fh, off) == FR_OK && f_write(&fh, d, n, &w) == FR_OK && w == n;
}

/* D64 error table, created on the first bad block (all ok), so the order
   of the blocks does not matter */
static bool etab_mark(uint16_t n, uint8_t code)
{
  if (type != XIMG_D64)
    return true;
  if (!etab) {
    uint16_t i;
    uint8_t one[32];
    if (code == 1)
      return true;
    etab = (uint32_t)blocks() * 256UL;
    memset(one, 1, sizeof(one));
    for (i = 0; i < blocks(); i += sizeof(one)) {
      UINT k = blocks() - i < sizeof(one) ? blocks() - i : sizeof(one);
      if (!img_write(etab + i, one, k))
        return false;
    }
  }
  return img_write(etab + n, &code, 1);
}

bool ximg_put_block(uint8_t sector, uint8_t result)
{
  uint32_t o = ximg_offset(type, track, sector);
  if (result != 1)
    memset(blk->data, 0, 256);
  return img_write(o, blk->data, 256) && etab_mark((uint16_t)(o / 256), result);
}

bool ximg_get_block(uint8_t sector, uint8_t sum[2])
{
  UINT r;
  if (f_lseek(&fh, ximg_offset(type, track, sector)) != FR_OK ||
      f_read(&fh, blk->data, 256, &r) != FR_OK || r != 256)
    return false;
  ximg_checksum(blk->data, sum);
  return true;
}

void ximg_finish(bool ok, uint16_t bad)
{
  if (bad > 255)
    bad = 255;
  ximg_armed_dev = 0;
  ximg_go = false;
  if (fh_open && mode == 'B')
    f_sync(&fh);
  set_error_ts(ok ? ERROR_OK : ERROR_DRIVE_NOT_READY, track, (uint8_t)bad);
}
