/*****************************************************************************

                      Omniprom Eprom Driver - C Version
                                     By
                                                       
	       RCL9, Dec. 26/84
 
	   (Rob's Retro Computing Archive) 
	RetroComputingArchive@gmail.com
		github.com/rcl9

                        C Conversion October 4, 1989.

******************************************************************************

History:
	1.8c - 10/04/89	- Derived from version 1.8 of omni.mac

Note: The Verify Flag will cause the Vpp to be turned off
      at the socket, so there is no way of having Vpp applied
      during a verify (which is needed for the 27256).

Note: It is not possible to program a byte then verify it then
      go on to program the next byte. What happens is that the
      verify operation causes the bus to change direction, which
      causes the 8255 to reset, causing the TOGGLE line to flip
      which in turn causes the counter to pre-maturely increment.
      There is no way around this....

*/

#define		TRUE	-1
#define		FALSE	0
#define		CR	0xd
#define		LF	0xa

#define		CPM	TRUE		/* TRUE for Z-80 CP/M Machines */

#ifdef CPM
#include	"stdio.h"
#include	"setjmp.h"		/* For setjmp, longjmp */
#else
#include	<stdio.h>
#include	<setjmp.h>		/* For setjmp, longjmp */
#endif

/* --------------- Modified when adding new computer installation ------------ */

#define	SORCERER	1	/* Adm 3A, 2mhz, S100 I/O 8255 interface */
#define	PIEDPIPER	2	/* Hazeltine, 4Mhz, Expansion board 8255 */
#define	COLOSSUS	3	/* 4 mhz, lcd screen (presently VT52 clear screen) */
#define	CYPHER		4	/* 4 Mhz, VT52, Expansion Board 8255 */

/* This is port base+4 for the 8255 I/O chip. Ie, if it is addressed for */
/* 252, then base will be 248. */

int	base[] = {192,		/* Sorcerer */
		248,		/* Pied Piper */
		0x24,		/* Colossus */
		29};		/* Cypher (on odd bytes) */

/* --------------------------------------------------------------------------- */

/* General storage */

char	buffer[84];		/* Input buffer */
char	*stdbf = (char *) NULL;	/* Eprom image buffer */
char	*pgm_addr;		/* programming address variable */
long	length = 0L;		/* current eprom length */
long	temp_length;		/* modifiable eprom length */
long	start = 0L; 		/* start of rom working on */
long	romend = 0L; 		/* pointer to end of rom using */
long	dump_offset = 0;	/* Last dump location */
char	pulse = 0;		/* current eprom pulse length (* 500 usec) */
char	repeat = 0;		/* number of times to pulse current eprom bytes */
char	eprminfo = 0; 		/* eprom bit programming info */
char	computer = 0;		/* type of computer in use */
char	baseport = 0;		/* holds 8255 baseport address */
int	toggle = 0;		/* Toggle state */
int	holder = 0;		/* Blinking state */
char	x = 0; 			/* error location */
char	tn = 0; 		/* current eprom type */
char	cksum = 0;		/* checksum storage */
char	temp_checksum = 0;	/* temporary checksum */
char	inout = 0; 		/* programmer in/out data buss flag */
char	flags = 0;		/* programmer current control word */
jmp_buf	abort_addr;		/* Long jump abort address for main routine */
jmp_buf	mini_addr;		/* Long jump abort address for monitor */

/* Place the number of eproms defined here */
#define	NUM_EPROMS	8

/* The name of each defined eprom goes here. */

char	*names[] = {
	"2704",
	"2708",
	"Intel 2716",
	"2732/2532",
	"2732A/462732",
	"Intel 2764",
	"Intel 27128",
	"Intel 27256" };

/*
First byte is number of bytes in the eprom (ie 1024)
Second byte is length of programming pulse in 500 usec inc's
Third byte is the # of times each byte will be programmed
Fourth byte contains info on the specific eprom used by the
  programming algorithm. It is bit mapped as:

	bit 1 = the pgm routine uses the 27256 
	        programming algorithm. Also, the CE* pin
		is controlled by the PGM* strobe line, so all
		reads require PGM* line low (bit 6 of port C)

	bit 3 = the pgm routine uses 2764/27128 programming
		(the pgm* pin is used to pulse).
		(don't change this, this mask is used by pgm routine)
*/

struct Rom_Data {
	long	length;			/* Eprom length */
	char	pulses;			/* Number of 500us pulses */
	char	repeat;			/* Number of times to repeat pulsing */
	int	prg_info;		/* Programming information */
	char	*voltage;		/* Programming voltage */
} romdata[] = {
	{ 512L,  1, 180, 0, "25" },	/* 2704 */
	{ 1024L, 1, 180, 0, "25" },	/* 2708 */
	{ 2048L, 10, 10, 0, "25" },	/* Intel 2716 */	
	{ 4096L, 10, 10, 0, "25" },	/* AM 2732 */
	{ 4096L, 10, 10, 0, "21" },	/* 2732A - HN462732G */
	{ 8192L,  4, 25, 4, "21" },	/* Intel 2764 */			
					/* Use pgm* to pulse, keep pulse */
					/* line active to keep 21v on. */
	{ 16384L, 4, 25, 4, "21" },	/* Intel 27128 */
					/* Use pgm* to pulse, keep pulse */
					/* line active to keep 21v on. */
	{ 32768L, 2, 25, 2, "12.5" }	/* Intel 27256 - use a modified programming strategy */
};					/* 1ms program pulses & PGM* pine connected to CE* */

/* Error messages */

char	*err_msgs[] = {
	"Function complete - eprom data ok.",
	"Checksum error - check ram data.",
	"Error: Erase Eprom.",
	"Error: Won't program.",
	"Verify error, reprogram eprom.",
	"Ok, eprom erased.",
	"Eprom read error - repeat function.",
	"Error: eprom not erased.",
	"(empty)",
	"Error opening file.",
	"Error while writing data.",
	"Error while reading data (top of memory?).",
};

/* --------------------------------------------------------------------- */

main() {
	char	c;

	printf("\n\nHost computer?\n\n");
	printf("\t1) Sorcerer (S100 parallel connector)\n");
	printf("\t2) Pied Piper (Expansion board parallel port)\n");
	printf("\t3) Colossus + VT52 Terminal\n");
	printf("\t4) Cypher + VT52 Terminal\n\n");
	printf("> ");

	c = getkey_bounded('1', '4');
	computer = c - '0';
	baseport = base[c - '1'];

	clr_screen();
	printf("\t(---------------------------------------------------)\n");
	printf("\t( O M N I P R O M   E P R O M   P R O G R A M M E R )\n");
	printf("\t(                  By RCL9, github.com/rcl9               )\n");
	printf("\t(              Version 1.8c - October 1989          )\n");
	printf("\t(---------------------------------------------------)\n\n");

	/* Initialize the I/O ports */
	Init();
	/* Init asterisk for blinking */
	holder = 0x20;
	tn = 0;
	get_type();

	/* Aborts end up here */
	setjmp(abort_addr);

	while (TRUE) {
		clr_screen();
		if (!tn)
			printf("No type selected.\n");
		else 
			printf("Current eprom selected: %s\n", names[tn - 1]);

		printf("\nSelect option:\n\n");
		printf("   0 - Clear buffer to 0FFh\n");
		printf("   1 - Set type\n");
		printf("   2 - Check Eprom for erased condition\n");
		printf("   3 - Read Eprom to buffer\n");
		printf("   4 - Verify Eprom with buffer\n");
		printf("   5 - Mini-monitor\n");
		printf("   6 - Read/write disk file\n");
		printf("   7 - Program Eprom\n");
		printf("   8 - Exit\n\n");
		printf(">");

		switch (getkey_bounded('0', '8')) {
			case '0':
				Clearbuf();	/* Clear buffer to FFh */
				break;
			case '1':
				clr_screen();	/* Get eprom type */
				get_type();
				break;
			case '2':
				Check_empty();	/* Check for erased eprom */
				break;
			case '3':
				Read_Eprom();	/* Read eprom routine */
				break;
			case '4':
				Verify();	/* Verify function */
				break;
			case '5':
				Monitor();	/* Memory modify */
				break;
			case '6':
				Disk_RW();	/* Disk read */
				break;
			case '7':
				Program();	/* Program eprom */
				break;
			case '8':
				Quit();		/* Exit to DOS */
			default:
				printf("Case error in main()\n");
		}
	} /* end while(TRUE); */
}

abort()
{
	longjmp(abort_addr, -1);
}

/* Ask user for eprom type */

get_type()
{		
	long	i;

	/* Zero eprom type (none selected) */
	tn = 0;
	printf("Eprom types available:\n\n");

	for (i = 0; i < NUM_EPROMS; i++) {
		printf("%ld - %-14s", i+1L, names[i]);
		printf(" - %sv\n", romdata[i].voltage);
	}

	printf("\nNote: 27C256 voltage may have to backed off from current setting, and NEC\n");
	printf("      27128 voltage may have to be increased (use Voltmeter on Vpp pin and\n");
	printf("      adjust for average value).\n");

	printf("\nEnter type number? ");
	tn =  getkey_bounded('1', '0' + NUM_EPROMS) - '0';

	length = romdata[tn - 1].length;	/* Eprom length */
	pulse = romdata[tn - 1].pulses;		/* Length of pulse in 500us increments */
	repeat = romdata[tn - 1].repeat;	/* Number of times to repeat pulse */
	eprminfo = romdata[tn - 1].prg_info;	/* Programming information */

	/* Allocate memory for the eprom image buffer */
	if (stdbf != (char *) NULL)
		free((char *) stdbf);
	if ((stdbf = malloc((int) length)) == (char *) NULL) {
		printf("\nCould not allocate the %ld bytes required.\n", length);
		exit(1);
	}
	/* Initialize the buffer to 0xff */
	clear_buf();
}

/*  Clear eprom buffer routine */

Clearbuf()
{
	clr_screen();
	printf("\nInitialize Buffer to 0FFh.\n\n");
	if (wait_for_x_or_g() == 'X')
		return;
	clear_buf();
	printf("\n");
	spacebar_and_abort();
}

clear_buf()
{
	long	i;
	char	*temp;

	/* Initialize the buffer to 0xff */
	temp = stdbf;
	for (i = 0; i < length; i++)
		*(temp++) = 0xff;
}

/* Read eprom routine */

Read_Eprom()
{	
	clr_screen();
	if (!tn)
		return;
	printf("Read Eprom routine.\n\n");

	printf("Buffer Start (Press RETURN for standard buffer)?\n");
	printf("--> ");
	if (gethex() == 4)
		start = 0L;
	else
		sscanf(buffer, "%lx", &start);	
	printf("\n\nDevice will load to %lxh - %lxh.\n", start, start+length-1L);
	insert_eprom();
	if (wait_for_x_or_g() == 'X')
		abort();
	/* Read in the eprom */
	read();
	printf("Read complete\n\n");
	spacebar_and_abort();
}

spacebar_and_abort()
{
	printf("\nPress any key to return to menu.");
	getkey();
	abort();
}

/* Wait for user to type X or G (exit or go) */

wait_for_x_or_g()
{
	char	c;

	printf("\nPress 'G' to continue, 'X' to abort.");
	do {
		c = getkey();
	} while (c != 'G' && c != 'X');

	if (c == 'G')
		printf("\r                                                   \n");
	return(c);
}

/* Verify function */

Verify()
{
	clr_screen();
	if (!tn)
		return;
	printf("\nVerify Eprom with buffer contents.\n\n");
	/* Tell to insert eprom */
	insert_eprom();
	if (wait_for_x_or_g() == 'X')
		return;
	/* Go verify the eprom */
	prt_cmd_status(verif());
}

prt_cmd_status(a)
{
	printf("\n\n%s\n", err_msgs[a]);
	spacebar_and_abort();
}

/* Check eprom for erased condition */

Check_empty()
{	
	clr_screen();
	if (!tn)
		return;
	printf("\nCheck Eprom for erased condition.\n\n");

	/* Ask to insert eprom */
	insert_eprom();
	if (wait_for_x_or_g() == 'X')
		return;
	/* Check for erased eprom */
	prt_cmd_status(eras());
}
	
/* Program eprom */

Program()
{	
	clr_screen();
	if (!tn)
		return;
	printf("\nProgram Eprom.\n\n");
	printf("\nPlace %s in socket.\n", names[tn - 1]);
	printf("Turn program enable OFF (down).\n");
	printf("Turn on power, wait 4 seconds then turn on program enable.\n");
	if (wait_for_x_or_g() == 'X')
		return;
	/* Program the eprom */
	prt_cmd_status(pgm());
}

/* Exit to DOS */

Quit()
{
	if (stdbf != (char *) NULL)
		free((char *) stdbf);
	printf("\n\nExiting to DOS.\n");
	exit(0);
}

insert_eprom()
{
	printf("\nPlace %s in socket.\n", names[tn - 1]);
	printf("Turn program enable off (down).\n");
	printf("Turn power ON. Keep program enable OFF!!\n");
}

/* Mini-monitor */

Monitor()
{
	clr_screen();
	if (!tn)
		return;
	printf("\n\nMini-Monitor.\n\n");
	printf("\nCommands:\n\n");
	printf("M XXXX YYYY ZZZZ  --> Move memory range (XXXX-YYY) to ZZZZ.\n");
	printf("M XXXX ZZZZ SAAAA --> Move XXXX to ZZZZ, block length = AAAA.\n");
	printf("D XXXX YYYY       --> Dump memory from (XXXX-YYYY).\n");
	printf("E XXXX            --> Enter data into memory at XXXX.\n");
	printf("X                 --> Exit.\n\n");

	setjmp(mini_addr);

	while (TRUE) {
		printf("> ");
		linein();

		switch (buffer[0]) {
			case 'M':
				Move();
				cksm1();	/* checksum on buffer */
				break;
			case 'E':
				Enter();
				cksm1();	/* checksum on buffer */
				break;
			case 'D':
				Dump();
				break;
			case 'X':
				return;
			case CR:
				break;
			default:
				printf("\n\nCommands are (M)ove, (E)nter, (D)ump, e(X)it.\n");
		}
	}
}

/* Disk read/write */

Disk_RW()
{
	int	c;

	clr_screen();
	if (!tn)
		return;
	printf("\n\nRead/Write disk file.\n\n");
	printf("(R)ead, (W)rite or e(X)it ? ");

	do {
		c = getkey();
	} while (c != 'R' && c != 'W' && c != 'X');

	printf("%c\n\n", c);
	if (c == 'X')
		return;

	if (c == 'R')
		printf("Load ");
	else
		printf("Save ");

	printf("start offset (hex) -- hit RETURN for start of buffer:\n");
	printf("--> ");
	if (gethex() == 4)
		start = 0L;
	else
		sscanf(buffer, "%lx", &start);	
	printf("\n\n");

	if (c == 'R')
		load();
	else {
		printf("Save end (hex) -- hit RETURN for Eprom length:\n");
		printf("--> ");
		if (gethex() == 4)
			romend = start + length - 1L;
		else
			sscanf(buffer, "%lx", &romend);	
		printf("\n");
		save();
	}
}

save()
{
	FILE	*fp;

	getfname();
	if ((fp = fopen(buffer, "w")) == (FILE *) NULL) 
		prt_cmd_status(9);
	if (fwrite(stdbf+start, 1, (int) (romend-start+1), fp) != (int) (romend-start+1))
		prt_cmd_status(10);
	if (fclose(fp) == -1)
		prt_cmd_status(12);
	/* Terminate normally */
	prt_cmd_status(0);
}

load()
{
	FILE	*fp;

	getfname();
	if ((fp = fopen(buffer, "r")) == (FILE *) NULL) 
		prt_cmd_status(9);
	if (fread(stdbf+start, 1, (int) (length - start), fp) != (int) (length-start))
		prt_cmd_status(11);
	if (fclose(fp) == -1)
		prt_cmd_status(12);
	/* Do checksum on buffer */
	cksm1();
	/* Terminate normally */
	prt_cmd_status(0);
}

getfname()
{
	printf("\nInput filename, of form 'du:filename.typ':\n");
	printf(">");
	linein();
	if (!strlen(buffer) || buffer[0] == CR || buffer[0] == LF)
		abort();
}

/* Input a 4 character hex address into 'buffer' */

gethex()
{
	char	c;
	int	count, offset;

	offset = 0;
	count = 4;

	while (TRUE) {
		if ((c = getkey()) == 'X')
			abort();

		/* Backspace or delete? */
		if (c == 8 || c == 0x7f) {
			if (count == 4)
				continue;
			count++;
			putchar(8);
			putchar(0x20);
			putchar(8);
			buffer[offset--] = 0;
			continue;
		} 

		buffer[offset] = c;
		buffer[offset+1] = 0;

		if (c == '\r')
			return(count);

		if (!count)
			continue;

		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
			putchar(c);
			offset++;
			count--;
		}
	}
}

/* Dump memory routine */

Dump()
{
	long	start_offset, end_offset, i;
	long	byte_count;
	int	arg_count;

	arg_count = sscanf(buffer+1, "%lx %lx", &start_offset, &end_offset);

	if (arg_count == 1)
		byte_count = 256;
	else if (arg_count == 2) 
		byte_count = end_offset - start_offset + 1;
	else {
		printf("%lx: %x\n", dump_offset, *(stdbf+dump_offset++));
		return;
	}

	dump_offset = start_offset;
	printf("\nAddr   0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F\n\n");

/*
dump2:	call	quik
	cp	'S'-40h		; pause output with ^S or Spacebar
	jr	z,dump3a
	cp	020h
	jr	z,dump3a
	or	a
	ret	nz		; return if user presses any key
	jr	dump4

dump3a:	call	quik
	cp	'S'-40h		; pause output with ^S or Spacebar
	jr	z,dump3a
	cp	020h
	jr	z,dump3a
	or	a
	ret	nz		; return if user presses any key
*/

	printf("%lx: ", dump_offset);
	for (i = 0; i < byte_count; i++) {
		printf(" %x", *(stdbf + dump_offset++));
		if (i % 16)
			printf("\n");
		if (i % 3)
			printf(" ");
	}
	printf("\n");
}

/* Enter hex.byte routine */

Enter()
{
	long	start_offset;
	int	byte, a, b;

	if (sscanf(buffer+1, "%lx", &start_offset) != 1)
		errpar();

	while (TRUE) {
		printf("\n%lx: %x", start_offset, *(stdbf + start_offset));
		
		do {
			a = getkey();
			if (a == '-') {
				printf("\n");
				start_offset--;
				continue;
			}
			if (a == CR) {
				printf("\n");
				start_offset++;
				continue;
			}
			if (a == '.')
				return;
		} while (!((a >= '0' && a <= '9') || (a >= 'A' || a < 'F')));

		do  {
			b = getkey();
		} while (!((b >= '0' && b <= '9') || (b >= 'A' || b < 'F')));

		*(stdbf + start_offset++) = a * 16 + b;
	}
}

/* Move block routine */

Move()
{
	long	from_offset, end_offset, to_offset, count, i;
	int	a;

	if (sscanf(buffer+1, "%lx %lx s%lx", &from_offset, &to_offset, &count) != 3) {
		if (sscanf(buffer+1, "%lx %lx %lx", &from_offset, &end_offset, &to_offset) == 3) {
			count = end_offset - from_offset + 1;
		} else
			errpar();
	}

	for (i = 0; i < count; i++) {
		a = stdbf[from_offset++];
		stdbf[to_offset++] = a;
	}
}

errpar()
{
	printf("Parameter error.\n");
	longjmp(mini_addr, -1);
}

/* Input a line to buffer from the console */

linein()
{
	int	i;

printf("Starting linein");
	fgets(&buffer, 80, stdin);
printf("ending linein");

	/* Convert buffer to capitals */
	for (i = 0; i < strlen(buffer); i++)
		buffer[i] = toupper(buffer[i]);
}

/* Get a valid keystroke between two bounds */

getkey_bounded(a, b)
	char	a, b;
{
	char	c;

	do {
		c = getkey();
	} while (c < a || c > b);
	return(c);
}

toupper(c)
	char	c;
{
	if (c >= 'a' && c <= 'z')
		return(c - 0x20);
	else
		return(c);
}

/*  ----- lowlevel sub-subroutines to menu subroutines -------- */

pgm()
{
	int	error, count, repeat1, repeat2;

	toggle = 0;
	pgm_addr = stdbf;
	repeat1 = repeat;	/* Get reprogram count */
	repeat2 = repeat >> 2; /* Reprogram count after good verify */

#if 0


	call	tget		; get 'T' parameters: bc=length
				; D=pulse length, E=reprogram count
				; HL = bufst

loop:		/* Use special routine for Intel 2756 */
		if (eprminfo & 2)
			intelprm();
n^^^^^^^^ fix -^^^^^^^^^
		
		printf("\nProgramming..\n\n");

		/* Do program loop */
		ppgm();
		printf("\nVerifyinç data..\n\n");
		if (x = verif1()) {		/* check data */
			if (x == 2)
				return(2);
			if (--repeat1 > 0)
				goto loop:
			/* Won't program */
			return(3);
		} else {

noerr:	exx			; alt
	ld	a,b		; remaining program count
	and	c		
	jr	z,v1		; if either count 0, then quit
	ld	a,b		
	cp	c		; if b<c theen use b
	jr	c,pgm1
	ld	b,c		; C is smaller
pgm1:	exx			; norm
	call	ppgm		; program loop
	exx			; alt
	djnz	pgm1		; do loop for B count

v1º	if (!(error = verif±()))	/* comparå data */
		/* Do checksum */
		if (cksum != temp_checksum)
			error = 1;
		else
			cksum = temp_checksum;
	return(error);

#endif
}

/* To toggle bit 1, cntrl has to be called twice since the */
/* counter is toggled on the edge. */

ppgm()
{
	/* Reset counter, verify off */
	cntrl(1);
	toggle = 0;
	pgm_addr = stdbf;
	/* Hold on a moment */
	dly(pulse);
	/* See if we should wait 2uses after sending program pulse */
	/* (used by 2764 & 27128 timing). */
	if ((eprminfo & 4) == 4)
		cntrl(4);		/* Turn on 21v to chip */

	temp_length = length;
	while (TRUE) {
		/* Send data to eprom */
		put(*pgm_addr);
		/* Delay 10us then blink */
		blink();

		if ((eprminfo & 4) != 4)
			/* Turn on pulse line (low) */
			cntrl(4);		
		else
			/* If 2764, bring pgm* low */
			cntrl(0x44);

		dly(pulse);			/* program pulse delay */

		/* 4 (21v on) or 0 = end pulse, end pulse */
		cntrl(eprminfo & 4);

		pgm_addr++;
		if (!(--temp_length))
			return;

		/* Set bit 2 (toggle) so D will toggle and will pulse the counter */
		cntrl((eprminfo & 4) | 2);
	}
}

/*	----- 27256k Eprom Programming Algorithm ------

 This performs the same algorithm as above, but toggles different
 control lines (it does all programming 1st, then does the verify).

*/

intelpgm()
{
	long	i;

retry:	printf("\nProgramming only non-blank cells....\n");
	toggle = 0;
	pgm_addr = stdbf;

	/* Find the first byte of the block of FF's at the end of the file */
	temp_length = length;
	for (i = length; i; --i)
		if (stdbf[i-1] != 0xff) {
			temp_length = i;
			break;
		}

	/* And start the programming routine */

	/* Reset counter, verify off */
	cntrl(1);
	toggle = 0;
	/* (4) 12.5v on, CE* off */
	cntrl(0x44);
	/* Wait 10ms for power to stabilize */
	dly(20);

	/* Blink asterisk only every 1000h characters */
intel2: if (temp_length ^ 0x700)
		/* 10usec delay then blink */
		blink();

	/* Program the byte by pulsing the CE* line */

	/* Send to eprom if not 0xff */
	if ((*pgm_addr) != 0xff) {
		put(*pgm_addr);
		/* CE* on, 12.5v on */
		cntrl(4);
		/* Program pulse delay (5ms) */
		dly(10);
		/* 12.5v on, CE* off - end pulse */
		cntrl(0x44);
	}

	pgm_addr++;
	if (--temp_length) {
		/* Set bit 2 (toggle) so D will toggle, increment hardware counter */
		/* 12.5v on, CE* off & pulse counter */
		cntrl(0x44 | 2);
		goto intel2; 
	} else {
		/* Now do a verify on the eprom, and do a checksum */
		printf("\nVerifyinç data....\n");
		if (!(x = verif1())) {
			/* No error, now fix up the checksum */
			if (cksum != temp_checksum)
				prt_cmd_status(1);
			else {
				cksum = temp_checksum;
				prt_cmd_status(0);
			}
		}
		printf("\nProgramming error, try to reprogram (Y/N)?");
		if (getkey() == 'Y') {
			printf("\n");
			goto retry;
		}
		prt_cmd_status(x);
	}
}

/* Do verify and compute checksum of eprom into temp_checksum */

verif1()
{
	int	val;

	/* Put progammer into verify (read) mode */
	cntrl(8);
	/* Wait a bit */
	dly(pulse);
	/* Reset counter */
	cntrl(9);
	toggle = 0;
	pgm_addr = stdbf;
	temp_checksum = 0;
	temp_length = length;
	while (TRUE) {
		cntrl(8);
		/* Read byte from eprom */
		val = inpro();
		temp_checksum += val;
		if (val ^ *pgm_addr)
			/* Programming error */
			return(4);
		/* Pulse counter */
		cntrl(10);
		pgm_addr++;
		temp_length--;
		if (!temp_length)
			return(0);
	}
}

/* Low-level eprom read routine */

read()
{
	int	error;

	/* Reset counter */
	cntrl(9);
	toggle = 0;
	temp_checksum = 0;
	temp_length = length;
	pgm_addr = stdbf;
	while (TRUE) {
		cntrl(8);
		/* Read byte from eprom */
		*(pgm_addr++) = inpro();
		/* Pulse counter */
		cntrl(10);
		temp_length--;
		if (!temp_length)
			return(0);
	}

	/* Verify buffer and perform a checksum on eprom */	
	if (error = verif1())
		prt_cmd_status(error);
	if (temp_checksum != cksum)
		/* Checksum error */
		prt_cmd_status(1);
	cksum = temp_checksum;
	prt_cmd_status(0);
}

/* Do checksum on the buffer */

cksm1()
{
	toggle = 0;
	pgm_addr = stdbf;
	cksum = 0;
	temp_length = length;
	while (TRUE) {
		/* Read byte from eprom */
		cksum += *(pgm_addr++);
		if (!(--temp_length))
			return;
	}
}

/* Low-level eprom verify routine */

verif()
{
	/* Read eprom and do a checksum */
	if (!verif1()) {
		/* Do checksum */
		if (cksum != temp_checksum)
			return(1);
		else
			cksum = temp_checksum;
	}
	return(0);
}

/* Low-level eprom erase check */

eras()
{
	/* Reset counter */
	cntrl(9);
	toggle = 0;
	pgm_addr = stdbf;
	temp_checksum = 0;
	temp_length = length;
	while (TRUE) {
		cntrl(8);
		/* Read byte from eprom */
		if (inpro() != 0xff)
			/* Eprom not erased */
			return(7);
		/* Pulse counter */
		cntrl(10);
		temp_length--;
		if (!temp_length)
			return(0);
	}
	/* Eprom is erased */
	return(5);
}

inpro()
{
	dly(1);
	return(get());
}


/* ------------- System dependent subroutines ----------------- */

/* Initialize I/O hardware , make 8255 Port A output, C input */
/* then set direction of bidirectiional port on eprom */
/* programmer to input. */

Init()
{
	/* Set in/out flag to input */
	inout = 1;
	/* Reset the counter */
	flags = 9;
	/* Make 8255 port A & C output */
	putout(7, 137);
	input();
	/* Set initial toggle state = 0 */
	toggle = 0;
}

/* Make Port C of 8255 output and programmer buss to output */

output()
{
	int	 temp;

	/* Signify output mode */
	inout = 0;
	/* Get current control word */
	temp = flags & 0xcf;
	/* Disable programmer bus  & complement reset bit 0 via port A */
	putout(4, invert(temp | 0x30));
	/* Set 8255 Port A & C to output */
	putout(7, 128);
	/* Enable programmer buss to output */
	flags = invert(temp | 0x10);
	putout(4, flags);
}

/* Make Port C of 8255 input and programmer buss to input */

input()
{
	int	 temp;

	/* Signify input mode */
	inout = 1;
	/* Get current control word */
	temp = flags & 0xcf;
	/* Disable programmer bus  */
	putout(4, invert(temp | 0x20));
	/* Set 8255 Port A to output, port C to input */
	putout(7, 137);
	/* Enable programmer buss to output */
	flags = invert(temp);
	putout(4, flags);
}

/* Invert bits 0-3 of A */

invert(a)
	int	a;
{
	return((a & 0xfff0) | ((~a) & 0xf));
}

/* Send control byte to control register (Port A). */

/* Toggle to alternate state by setting bit 2 of A */
/* but the actual counter only increments the count every other */
/* byte (toggle goes low, high, then inc's on falling edge of */
/* next transition). */

cntrl(val)
{
	int	b;

	/* Check verify bit */
	if (val & 8) {
		if (inout != 1)
			/* Change bus to input */
			input();
	} else {
		if (inout != 0)
			/* Change bus to output */
			output();
	}

	/* Mask bits 4,5,7 then mask on direction, enable and pgm* bits */
	b = (val & 0x4f) | (flags & 0x30);

	/* If not reset, toggle */
	if (!(b & 1))
		b ^= toggle;

	/* Send control nybble */
	putout(4, invert(b));
	/* Output again, but set reset bit to 1 (off) */
	putout(4, invert(b) + 1);

	/* Update the flags image */
	flags = b & 0xfe;

	/* Save the toggle state */
	toggle = b & 2;
}

/* Input byte to A from Port C (from programmer data buss) */

get()
{
	/* Make sure programmer data bus is in the input mode */
	if (inout != 1)
		input();
	/* Input from port C */
	return(getinp(6));
}

/* Output A to Port C (goes to programmer data buss) */

put(a)
	int	a;
{
	/* Make sure programmer data bus is in the output mode */
	if (inout == 1)
		output();
	/* Output to port C */
	putout(6, a);
}

/* Input byte from port specified by offset */

getinp(offset)
	int	offset;
{
	int	port;

	if (computer != CYPHER) {
		/* Translate for Cypher odd byte addressing */
		port = baseport + 4 + baseport + (offset - 4) * 2;
	} else 
		port = baseport + offset;

	return(in(port));
}

/* Output byte from port specified by offset */

putout(offset, value)
	int	offset, value;
{
	int	port;

	if (computer != CYPHER) {
		/* Translate for Cypher odd byte addressing */
		port = baseport + 4 + baseport + (offset - 4) * 2;
	} else 
		port = baseport + offset;

	out(port, value);
}

/* Delay d * (.5ms) */

dly(d)
	int	d;
{
	/* T cycles = 7 + 65*(4+12) +7 of inner loop */

	if (computer == SORCERER) {
#asm
	lxi	h,2
	dad	sp
	push	b
	mov	b,m		; pick up delay argument

dly1:	mvi	c,65		; 		7      | must add to .5ms
dly2:	dcr	c		; 		4      |
	db	20h, 0fdh	; jr nz, dly2 	12/7   |
	db 	10h, 0f9h	; djnz 	dly1	13 (b<>0), 8 (b=0)
	pop	b
#endasm

	} else {
#asm
	lxi	h,2
	dad	sp
	push	b
	mov	b,m		; pick up delay argument

dly3:	mvi	c,124		; 		7      | must add to .5ms
dly4:	dcr	c		; 		4      |
	db	20h, 0fdh	; jr nz, dly4 	12/7   |
	db 	10h, 0f9h	; djnz 	dly3	13 (b<>0), 8 (b=0)
	pop	b
#endasm
	}
}

blink()
{
	if (computer == SORCERER) {
		/* Gives space-*-space */
		holder ^= 0xa;
		/* Place directly on the screen */
		*(0xf080 + 0x3e) = holder;
	} else {
		printf("\r %c", 8);
		/* Gives space-*-space */
		holder ^= 0xa;
		putchar(holder);
	}
}

/* Clear screen */

clr_screen() 
{
	switch(computer) {
		case SORCERER:
			putchar(0xc);
			break;
		case COLOSSUS:
		case CYPHER:
			putchar(0x1b);
			putchar('E');
			break;
		default:
			/* Pied Piper Clear Screen */
			putchar(0x7e);
			putchar(0x1c);
			break;
	}
}

/* Wait and return a key from the keybord. Convert to uppercase. */

getkey()
{
	char	c;

	while (!(c = bdos(6, 0xff)));
	return(toupper(c));
}
