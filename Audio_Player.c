/*=========================================================================*/
/*  Includes                                                               */
/*=========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <system.h>
#include <sys/alt_alarm.h>
#include <io.h>

#include "fatfs.h"
#include "diskio.h"

#include "ff.h"
#include "monitor.h"
#include "uart.h"

#include "alt_types.h"

#include <altera_up_avalon_audio.h>
#include <altera_up_avalon_audio_and_video_config.h>

/*=========================================================================*/
/*  DEFINE: All Structures and Common Constants                            */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Macros                                                         */
/*=========================================================================*/

#define PSTR(_a)  _a

/*=========================================================================*/
/*  DEFINE: Prototypes                                                     */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Definition of all local Data                                   */
/*=========================================================================*/
static alt_alarm alarm;
static unsigned long Systick = 0;
static volatile unsigned short Timer;   /* 1000Hz increment timer */

/*=========================================================================*/
/*  DEFINE: Definition of all local Procedures                             */
/*=========================================================================*/

/***************************************************************************/
/*  TimerFunction                                                          */
/*                                                                         */
/*  This timer function will provide a 10ms timer and                      */
/*  call ffs_DiskIOTimerproc.                                              */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static alt_u32 TimerFunction (void *context)
{
   static unsigned short wTimer10ms = 0;

   (void)context;

   Systick++;
   wTimer10ms++;
   Timer++; /* Performance counter for this module */

   if (wTimer10ms == 10)
   {
      wTimer10ms = 0;
      ffs_DiskIOTimerproc();  /* Drive timer procedure of low level disk I/O module */
   }

   return(1);
} /* TimerFunction */

/***************************************************************************/
/*  IoInit                                                                 */
/*                                                                         */
/*  Init the hardware like GPIO, UART, and more...                         */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static void IoInit(void)
{
   uart0_init(115200);

   /* Init diskio interface */
   ffs_DiskIOInit();

   //SetHighSpeed();

   /* Init timer system */
   alt_alarm_start(&alarm, 1, &TimerFunction, NULL);

} /* IoInit */

/*=========================================================================*/
/*  DEFINE: All code exported                                              */
/*=========================================================================*/

uint32_t acc_size;                 /* Work register for fs command */
uint16_t acc_files, acc_dirs;
FILINFO Finfo;
#if _USE_LFN
char Lfname[512];
#endif

char Line[256];                 /* Console input buffer */

FATFS Fatfs[_VOLUMES];          /* File system object for each logical drive */
FIL File1, File2;               /* File objects */
DIR Dir;                        /* Directory object */
uint8_t Buff[8192] __attribute__ ((aligned(4)));  /* Working buffer */




static
FRESULT scan_files(char *path)
{
    DIR dirs;
    FRESULT res;
    uint8_t i;
    char *fn;


    if ((res = f_opendir(&dirs, path)) == FR_OK) {
        i = (uint8_t)strlen(path);
        while (((res = f_readdir(&dirs, &Finfo)) == FR_OK) && Finfo.fname[0]) {
            if (_FS_RPATH && Finfo.fname[0] == '.')
                continue;
#if _USE_LFN
            fn = *Finfo.lfname ? Finfo.lfname : Finfo.fname;
#else
            fn = Finfo.fname;
#endif
            if (Finfo.fattrib & AM_DIR) {
                acc_dirs++;
                *(path + i) = '/';
                strcpy(path + i + 1, fn);
                res = scan_files(path);
                *(path + i) = '\0';
                if (res != FR_OK)
                    break;
            } else {
                //      xprintf("%s/%s\n", path, fn);
                acc_files++;
                acc_size += Finfo.fsize;
            }
        }
    }

    return res;
}


//                put_rc(f_mount((uint8_t) p1, &Fatfs[p1]));

static
void put_rc(FRESULT rc)
{
    const char *str =
        "OK\0" "DISK_ERR\0" "INT_ERR\0" "NOT_READY\0" "NO_FILE\0" "NO_PATH\0"
        "INVALID_NAME\0" "DENIED\0" "EXIST\0" "INVALID_OBJECT\0" "WRITE_PROTECTED\0"
        "INVALID_DRIVE\0" "NOT_ENABLED\0" "NO_FILE_SYSTEM\0" "MKFS_ABORTED\0" "TIMEOUT\0"
        "LOCKED\0" "NOT_ENOUGH_CORE\0" "TOO_MANY_OPEN_FILES\0";
    FRESULT i;

    for (i = 0; i != rc && *str; i++) {
        while (*str++);
    }
    xprintf("rc=%u FR_%s\n", (uint32_t) rc, str);
}

static
void display_help(void)
{
    xputs("dd <phy_drv#> [<sector>] - Dump sector\n"
          "di <phy_drv#> - Initialize disk\n"
          "ds <phy_drv#> - Show disk status\n"
          "bd <addr> - Dump R/W buffer\n"
          "be <addr> [<data>] ... - Edit R/W buffer\n"
          "br <phy_drv#> <sector> [<n>] - Read disk into R/W buffer\n"
          "bf <n> - Fill working buffer\n"
          "fc - Close a file\n"
          "fd <len> - Read and dump file from current fp\n"
          "fe - Seek file pointer\n"
          "fi <log drv#> - Force initialize the logical drive\n"
          "fl [<path>] - Directory listing\n"
          "fo <mode> <file> - Open a file\n"
    	  "fp -  (to be added by you) \n"
          "fr <len> - Read file\n"
          "fs [<path>] - Show logical drive status\n"
          "fz [<len>] - Get/Set transfer unit for fr/fw commands\n"
          "h view help (this)\n");
}

int isWav(char *filename) {
	if (filename[strlen(filename)-3] == 'W' &&	// check if file name ends in "wav"
		filename[strlen(filename)-2] == 'A' &&
		filename[strlen(filename)-1] == 'V') {
		return 1;
	}
	return 0;
}

char state[20];
void LCD(int index, char *data) {
	//LCD Display
	FILE *lcd;
	lcd = fopen("/dev/lcd_display", "w");
	fprintf(lcd, "%d-%s\n", index, data);
	fprintf(lcd, "%s\n", state);
}

char filename[20][20];
unsigned long fileSize[20];
long p1;
int speed;
int i;
int n;	// song index number
int play;	// to play or not
int reset;
static void labISR (void* context, alt_u32 id)
{
	IOWR(LED_PIO_BASE, 0, 2); // turn LEDs BIT1 to ON to denote START of ISR
	int button = IORD(BUTTON_PIO_BASE,0);
	if (button == 7){
		// back
		n -= 1;
		if (n < 0) {
			n += 20;
		}
		while (filename[n][0] == '\0') {
			n -= 1;
			if (n < 0) {
				n += 20;
			}
		}
		LCD(n+1,filename[n]);
		p1 = fileSize[n];
		reset = 1;
	} else if (button == 11) {
		// stop
		play = 0;
		reset = 1;
		strcpy(state,"Stopped");
		LCD(n+1,filename[n]);
	} else if (button == 13) {
		// pause/play
		play = !play;
		// printf("p %d. r %d\n",play, reset);
		strcpy(state,"Paused");
		if (play == 1) {
			strcpy(state,"Playing 1x");
			if (speed == 2) {
				strcpy(state,"Playing 1/2");
			} else if (speed == 8) {
				strcpy(state,"Playing 2x");
			}
		}
		LCD(n+1,filename[n]);
	} else if (button == 14) {
		// forward
		n += 1;
		n %= 20;
		while (filename[n][0] == '\0') {
			n += 1;
			n %= 20;
		}
		LCD(n+1,filename[n]);
		p1 = fileSize[n];
		reset = 1;
	}
	IOWR(BUTTON_PIO_BASE, 3, 0);	// clear interrupt flag
	IOWR(LED_PIO_BASE, 0, 0); // turn LEDs BIT1 to OFF to denote END of ISR
}

/***************************************************************************/
/*  main                                                                   */
/***************************************************************************/
int main(void)
{
	int fifospace;
    char *ptr, *ptr2;
    long p2, p3;
    uint8_t res, b1, drv = 0;
    uint16_t w1;
    uint32_t s1, s2, cnt, blen = sizeof(Buff);
    static const uint8_t ft[] = { 0, 12, 16, 32 };
    uint32_t ofs = 0, sect = 0, blk[2];
    FATFS *fs;                  /* Pointer to file system object */

    alt_up_audio_dev * audio_dev;
    /* used for audio record/playback */
    unsigned int l_buf;
    unsigned int r_buf;
    // open the Audio port
    audio_dev = alt_up_audio_open_dev ("/dev/Audio");
    if ( audio_dev == NULL)
    alt_printf ("Error: could not open audio device \n");
    else
    alt_printf ("Opened audio device \n");

    IoInit();

    IOWR(SEVEN_SEG_PIO_BASE,1,0x0007);

    xputs(PSTR("FatFs module test monitor\n"));
    xputs(_USE_LFN ? "LFN Enabled" : "LFN Disabled");
    xprintf(", Code page: %u\n", _CODE_PAGE);

    display_help();


#if _USE_LFN
    Finfo.lfname = Lfname;
    Finfo.lfsize = sizeof(Lfname);
#endif

    // Initialize 'di 0' and 'fi 0'
    (uint16_t) disk_initialize((uint8_t) 0);
    put_rc(f_mount((uint8_t) 0, &Fatfs[0]));

    // Song Index
    while (*ptr == ' ')
    	ptr++;
    f_opendir(&Dir, ptr);

    for (i = 0; i < 20; i++) {
    	f_readdir(&Dir, &Finfo);
    	strcpy(filename[i], Finfo.fname);
    	fileSize[i] = Finfo.fsize;
    	if (!isWav(filename[i]) && (filename[i][0]) != '\0') {
    		//printf("%d,%s \n",i,filename[i]);
    		//printf("%d \n",filename[i][0] == '\0');
    		//i -= 1;
    		filename[i][0] = '\0';
    	}
    }
    /*
    for (i = 0; i < 20; i++) {
    	printf("%d,%s \n",fileSize[i],filename[i]);
    }
	*/
    strcpy(state,"Stopped");
    for (i = 0; i < 20; i++) {
    	if (filename[i][0] != '\0') {
    		LCD(i+1,filename[i]);
    		break;
		}
	}

    n = i;
    p1 = fileSize[n];
    play = 0;
    reset = 1;

    alt_irq_register( BUTTON_PIO_IRQ, (void *)0, labISR );	// register ISR
    IOWR(BUTTON_PIO_BASE, 2, 15);	// enable (mask) ISR
    for (;;) {
    	int sw = IORD(SWITCH_PIO_BASE,0);	// check if switch is flipped, for double or half speed
    	speed = 4;	// default speed is 4 bytes per play
    	if (sw == 1){
    		speed = 2;	// half speed is 2 bytes per play
    	} else if (sw == 2) {
    		speed = 8;	// double speed is 8 bytes per play (skip every 4)
    	}

    	if (reset == 1) {
    		f_open(&File1, filename[n], 1);
    		p1 = fileSize[n];
    		reset = 0;
    	}

		while (p1 && play == 1) {
			if (reset == 1) {
				break;
			}
			//printf("%d,%d,%s\n",n,p1,filename[n]);
			cnt = 512;	// size of read stream buffer
			/*
			<<<<<<<<<<<<<<<<<<<<<<<<< YOUR fp CODE GOES IN HERE >>>>>>>>>>>>>>>>>>>>>>
			*/
			// copied logic from filedump portion
			// read every 512 bytes of file
			if ((uint32_t) p1 >= cnt)
			{
				p1 -=cnt;
			}
			else
			{
				cnt = p1;
				p1 = 0;
				play = 0;
				reset = 1;
				strcpy(state,"Stopped");
				LCD(n+1,filename[n]);
			}
			//printf("loc 2 %d\n",cnt);
			f_read(&File1, Buff, cnt, &cnt);

			alt_up_audio_dev * audio_dev;
			/* used for audio record/playback */
			unsigned int l_buf;
			unsigned int r_buf;
			//printf("%d, %d\n",p1, Buff);
			// open the Audio port
			audio_dev = alt_up_audio_open_dev ("/dev/Audio");
			//if ( audio_dev == NULL)
			//	alt_printf ("Error: could not open audio device \n");
			//else
			//	alt_printf ("Opened audio device \n");
			/* read and echo audio data */
			for(i = 0; i < cnt; i += speed)	// send in 4-byte increments
			{
				l_buf = (Buff[i+1]<<8)|(Buff[i]);	//	shift down before merging, merge based on Codec documentation
				r_buf = (Buff[i+3]<<8)|(Buff[i+2]);
				//printf("%d,%d\n",l_buf,r_buf);
				while(alt_up_audio_write_fifo_space (audio_dev, ALT_UP_AUDIO_RIGHT) == 0)
				{
					// wait for player to be available (or else it will click)
				}

				alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);	// play
				alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
			}
    	}
    }
    /*
     * This return here make no sense.
     * But to prevent the compiler warning:
     * "return type of 'main' is not 'int'
     * we use an int as return :-)
     */
    return (0);
	}
