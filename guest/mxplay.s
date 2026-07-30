*	Minimal MXDRV launcher for run68mpx
*	Usage: MXPLAY.X MXDRV.X MUSIC.MDX
*
MDX_CAPACITY	equ	65536
PDX_NAME_CAPACITY equ	512
KEY_POLL_DELAY	equ	50000

	.text
	.globl	start
start:
*	Release the unused tail of the root process block so EXEC can allocate
*	memory for MXDRV. A0 is the PSP block, and A1 is the program end.
	move.l	a1,d0
	sub.l	a0,d0
	subi.l	#16,d0
	move.l	d0,-(sp)
	clr.l	-(sp)			* current process block
	dc.w	$ff4a			* DOS _SETBLOCK
	addq.l	#8,sp
	tst.l	d0
	bmi	memory_error

	movea.l	a2,a0
	moveq	#0,d7
	move.b	(a0)+,d7
	lea	mxdrv_name(pc),a1
	bsr	next_arg
	tst.l	d0
	beq	usage
	lea	mdx_name(pc),a1
	bsr	next_arg
	tst.l	d0
	beq	usage

*	Start MXDRV as a child. MXDRV returns through KEEPPR and remains resident.
	clr.l	-(sp)			* inherit environment
	pea	empty_command(pc)
	pea	mxdrv_name(pc)
	clr.w	-(sp)			* EXEC mode 0
	dc.w	$ff4b			* DOS _EXEC
	lea	14(sp),sp
	tst.l	d0
	bmi	exec_error

*	Open the MDX and obtain its length.
	clr.w	-(sp)			* read-only
	pea	mdx_name(pc)
	dc.w	$ff3d			* DOS _OPEN
	addq.l	#6,sp
	tst.l	d0
	bmi	open_error
	move.w	d0,file_handle

	move.w	#2,-(sp)		* seek from end
	clr.l	-(sp)
	move.w	d0,-(sp)
	dc.w	$ff42			* DOS _SEEK
	lea	8(sp),sp
	tst.l	d0
	bmi	read_error
	move.l	d0,mdx_length
	cmpi.l	#MDX_CAPACITY,d0
	bhi	too_large

	clr.w	-(sp)			* seek from beginning
	clr.l	-(sp)
	move.w	file_handle(pc),-(sp)
	dc.w	$ff42
	lea	8(sp),sp
	tst.l	d0
	bmi	read_error

	move.l	mdx_length(pc),-(sp)
	lea	mdx_buffer(pc),a0
	lea	8(a0),a0		* leave room for MXDRV metadata
	move.l	a0,-(sp)
	move.w	file_handle(pc),-(sp)
	dc.w	$ff3f			* DOS _READ
	lea	10(sp),sp
	cmp.l	mdx_length(pc),d0
	bne	read_error

	move.w	file_handle(pc),-(sp)
	dc.w	$ff3e			* DOS _CLOSE
	addq.l	#2,sp

*	MXDRV LOADMML needs an 8-byte transfer header before the raw MDX.
	bsr	prepare_mdx
	tst.l	d0
	bmi	format_error
	move.l	d0,mdx_length

*	If the MDX names a PDX file, load it from the MDX directory and
*	transfer the MXDRV PCM wrapper before the MML wrapper.
	bsr	load_pdx
	tst.l	d0
	bmi	pdx_error
	beq	pdx_ready
	lea	mdx_buffer(pc),a0
	clr.w	2(a0)			* mark PDX number 0 as available
pdx_ready:

*	Install the MDX data, then start playback.
	lea	mdx_buffer(pc),a1
	move.l	mdx_length(pc),d1
	moveq	#$02,d0			* LOADMML
	trap	#4
	tst.l	d0
	bmi	load_error

	moveq	#0,d1			* MML number 0
	moveq	#$04,d0			* M_PLAY
	trap	#4
	lea	playing_message(pc),a1
	bsr	print

play_wait:
	move.l	#KEY_POLL_DELAY,d1
key_poll_delay:
	subq.l	#1,d1
	bne	key_poll_delay
	dc.w	$ff0b			* DOS _KEYSNS (non-blocking)
	tst.l	d0
	beq	play_wait
	dc.w	$ff07			* DOS _INKEY (consume the key)

play_done:
	moveq	#$05,d0			* M_END
	trap	#4
	lea	done_message(pc),a1
	bsr	print
	bra	exit_ok

usage:
	lea	usage_message(pc),a1
	bsr	print
	bra	exit_error

memory_error:
	lea	memory_message(pc),a1
	bsr	print
	bra	exit_error

exec_error:
	lea	exec_message(pc),a1
	bsr	print
	bra	exit_error

open_error:
	lea	open_message(pc),a1
	bsr	print
	bra	exit_error

too_large:
	lea	large_message(pc),a1
	bsr	print
	bra	close_and_error

read_error:
	lea	read_message(pc),a1
	bsr	print
close_and_error:
	move.w	file_handle(pc),-(sp)
	dc.w	$ff3e
	addq.l	#2,sp
	bra	exit_error

load_error:
	lea	load_message(pc),a1
	bsr	print
	bra	exit_error

format_error:
	lea	format_message(pc),a1
	bsr	print
	bra	exit_error

pdx_error:
	lea	pdx_message(pc),a1
	bsr	print
	bra	exit_error

exit_ok:
	clr.w	-(sp)
	dc.w	$ff4c			* DOS _EXIT2

exit_error:
	move.w	#1,-(sp)
	dc.w	$ff4c

*	Print the zero-terminated string in A1.
print:
	move.l	a1,-(sp)
	dc.w	$ff09
	addq.l	#4,sp
	rts

*	Copy the next space-delimited command-line argument to A1.
*	A0 points into the Human68k command line and D7 is the remaining length.
*	Returns D0=1 when an argument was copied, or D0=0 at end of input.
next_arg:
skip_spaces:
	tst.w	d7
	beq	arg_missing
	move.b	(a0)+,d0
	subq.w	#1,d7
	cmpi.b	#' ',d0
	beq	skip_spaces
	move.b	d0,(a1)+
copy_arg:
	tst.w	d7
	beq	arg_done
	move.b	(a0),d0
	cmpi.b	#' ',d0
	beq	consume_space
	move.b	(a0)+,(a1)+
	subq.w	#1,d7
	bra	copy_arg
consume_space:
	addq.l	#1,a0
	subq.w	#1,d7
arg_done:
	clr.b	(a1)
	moveq	#1,d0
	rts

*	Build an MDX-relative path for the embedded PDX name at A2.
make_pdx_path:
	tst.b	(a2)
	bne	pdx_name_present
	clr.b	pdx_name
	rts
pdx_name_present:
	movem.l	d0-d1/a0-a4,-(sp)
	movea.l	a2,a0
	lea	pdx_name(pc),a1
	lea	mdx_name(pc),a3
	movea.l	a1,a4
copy_mdx_path:
	moveq	#0,d1
	move.b	(a3)+,d1
	beq	path_prefix_done
	move.b	d1,(a1)+
	cmpi.b	#'/',d1
	beq	remember_separator
	cmpi.b	#$5c,d1
	beq	remember_separator
	cmpi.b	#':',d1
	bne	copy_mdx_path
remember_separator:
	movea.l	a1,a4
	bra	copy_mdx_path
path_prefix_done:
	movea.l	a4,a1
copy_pdx_leaf:
	move.b	(a0)+,(a1)+
	bne	copy_pdx_leaf
	movem.l	(sp)+,d0-d1/a0-a4
	rts

*	Append .PDX after an unsuccessful exact-name open.
append_pdx_extension:
	lea	pdx_name(pc),a0
find_pdx_name_end:
	tst.b	(a0)+
	bne	find_pdx_name_end
	subq.l	#1,a0
	move.b	#'.',(a0)+
	move.b	#'P',(a0)+
	move.b	#'D',(a0)+
	move.b	#'X',(a0)+
	clr.b	(a0)
	rts

*	Load and wrap the PDX named by prepare_mdx.
*	Returns D0=0 for no PDX, D0=1 when loaded, or D0=-1 on error.
load_pdx:
	tst.b	pdx_name
	beq	no_pdx
	move.w	#-1,pdx_handle
	clr.l	pdx_buffer_ptr
	clr.b	pdx_extension_tried

open_pdx:
	clr.w	-(sp)
	pea	pdx_name(pc)
	dc.w	$ff3d			* DOS _OPEN
	addq.l	#6,sp
	tst.l	d0
	bpl	pdx_opened
	tst.b	pdx_extension_tried
	bne	pdx_load_failed
	move.b	#1,pdx_extension_tried
	bsr	append_pdx_extension
	bra	open_pdx

pdx_opened:
	move.w	d0,pdx_handle
	move.w	#2,-(sp)
	clr.l	-(sp)
	move.w	d0,-(sp)
	dc.w	$ff42			* DOS _SEEK end
	lea	8(sp),sp
	tst.l	d0
	bmi	pdx_load_failed
	move.l	d0,pdx_file_length

	lea	pdx_name(pc),a0
	moveq	#0,d1
pdx_name_length_loop:
	addq.l	#1,d1
	tst.b	(a0)+
	bne	pdx_name_length_loop
	move.w	d1,pdx_name_length
	move.l	d1,d2
	addq.l	#8,d2
	addq.l	#1,d2
	andi.l	#$fffffffe,d2
	move.w	d2,pdx_body_offset
	add.l	pdx_file_length(pc),d2
	move.l	d2,pdx_total_length

	move.l	d2,-(sp)
	dc.w	$ff48			* DOS _MALLOC
	addq.l	#4,sp
	tst.l	d0
	bmi	pdx_load_failed
	move.l	d0,pdx_buffer_ptr
	movea.l	d0,a1
	clr.l	(a1)
	clr.l	4(a1)
	lea	pdx_name(pc),a0
	lea	8(a1),a2
copy_pdx_name:
	move.b	(a0)+,(a2)+
	bne	copy_pdx_name

	clr.w	-(sp)
	clr.l	-(sp)
	move.w	pdx_handle(pc),-(sp)
	dc.w	$ff42			* DOS _SEEK beginning
	lea	8(sp),sp
	tst.l	d0
	bmi	pdx_load_failed

	movea.l	pdx_buffer_ptr(pc),a0
	moveq	#0,d0
	move.w	pdx_body_offset(pc),d0
	adda.l	d0,a0
	move.l	pdx_file_length(pc),-(sp)
	move.l	a0,-(sp)
	move.w	pdx_handle(pc),-(sp)
	dc.w	$ff3f			* DOS _READ
	lea	10(sp),sp
	cmp.l	pdx_file_length(pc),d0
	bne	pdx_load_failed

	move.w	pdx_handle(pc),-(sp)
	dc.w	$ff3e			* DOS _CLOSE
	addq.l	#2,sp
	move.w	#-1,pdx_handle

	movea.l	pdx_buffer_ptr(pc),a1
	move.w	pdx_body_offset(pc),4(a1)
	move.w	pdx_name_length(pc),6(a1)
	move.l	pdx_total_length(pc),d1
	moveq	#$03,d0			* LOADPCM
	trap	#4
	move.l	d0,d5
	move.l	pdx_buffer_ptr(pc),-(sp)
	dc.w	$ff49			* DOS _MFREE
	addq.l	#4,sp
	clr.l	pdx_buffer_ptr
	move.l	d5,d0
	tst.l	d0
	bmi	pdx_load_failed
	moveq	#1,d0
	rts

no_pdx:
	moveq	#0,d0
	rts

pdx_load_failed:
	move.w	pdx_handle(pc),d0
	tst.w	d0
	bmi	pdx_not_open
	move.w	d0,-(sp)
	dc.w	$ff3e
	addq.l	#2,sp
	move.w	#-1,pdx_handle
pdx_not_open:
	move.l	pdx_buffer_ptr(pc),d0
	beq	pdx_not_allocated
	move.l	d0,-(sp)
	dc.w	$ff49
	addq.l	#4,sp
	clr.l	pdx_buffer_ptr
pdx_not_allocated:
	moveq	#-1,d0
	rts
arg_missing:
	clr.b	(a1)
	moveq	#0,d0
	rts

*	Build the MXDRV transfer header and return its total length in D0.
*	This follows the original MXP transformation: terminate and align the
*	title, discard CR/LF/$1a and the PDX name, then compact the MDX body.
prepare_mdx:
	lea	mdx_buffer(pc),a1
	lea	8(a1),a2		* raw MDX start/source
	movea.l	a2,a3			* title scanner
	movea.l	a2,a5
	adda.l	mdx_length(pc),a5	* one past raw MDX

find_title_end:
	cmpa.l	a5,a3
	bhs	mdx_bad
	moveq	#0,d1
	move.b	(a3)+,d1
	cmpi.b	#$0d,d1
	beq	terminate_title
	cmpi.b	#$0a,d1
	beq	terminate_title
	cmpi.b	#$20,d1
	bcc	find_title_end
	cmpi.b	#$1b,d1
	beq	find_title_end
	bra	mdx_bad

terminate_title:
	subq.l	#1,a3
	clr.b	(a3)+
	move.l	a3,d0
	btst	#0,d0
	beq	title_aligned
	cmpa.l	a5,a3
	bhs	mdx_bad
	clr.b	(a3)+

title_aligned:
	movea.l	a3,a4			* compacted body destination
	movea.l	a3,a2			* metadata source scanner
	cmpi.b	#$0d,d1
	beq	find_mdx_marker

find_carriage_return:
	cmpa.l	a5,a2
	bhs	mdx_bad
	cmpi.b	#$0d,(a2)+
	bne	find_carriage_return

find_mdx_marker:
	cmpa.l	a5,a2
	bhs	mdx_bad
	cmpi.b	#$1a,(a2)+
	bne	find_mdx_marker
	bsr	make_pdx_path

find_pdx_end:
	cmpa.l	a5,a2
	bhs	mdx_bad
	tst.b	(a2)+
	bne	find_pdx_end

	move.l	a4,d6
	sub.l	a1,d6			* compacted MDX body offset

copy_mdx_body:
	cmpa.l	a5,a2
	bhs	write_mdx_header
	move.b	(a2)+,(a4)+
	bra	copy_mdx_body

write_mdx_header:
	clr.l	(a1)
	move.w	#$ffff,2(a1)		* no PDX loaded yet
	move.w	d6,4(a1)
	move.w	#8,6(a1)
	move.l	a4,d0
	sub.l	a1,d0
	rts

mdx_bad:
	moveq	#-1,d0
	rts

	.data
empty_command:
	dc.b	0,0
usage_message:
	dc.b	'usage: MXPLAY.X MXDRV.X MUSIC.MDX',13,10,0
memory_message:
	dc.b	'cannot release process memory',13,10,0
exec_message:
	dc.b	'cannot start MXDRV.X',13,10,0
open_message:
	dc.b	'cannot open MDX file',13,10,0
large_message:
	dc.b	'MDX file is too large',13,10,0
read_message:
	dc.b	'cannot read MDX file',13,10,0
load_message:
	dc.b	'MXDRV rejected MDX data',13,10,0
format_message:
	dc.b	'invalid MDX structure',13,10,0
pdx_message:
	dc.b	'cannot load PDX file',13,10,0
playing_message:
	dc.b	'playing - press any key to stop',13,10,0
done_message:
	dc.b	'playback finished',13,10,0
	.even
file_handle:
	dc.w	-1
mdx_length:
	dc.l	0
pdx_handle:
	dc.w	-1
pdx_name_length:
	dc.w	0
pdx_body_offset:
	dc.w	0
	.even
pdx_file_length:
	dc.l	0
pdx_total_length:
	dc.l	0
pdx_buffer_ptr:
	dc.l	0
pdx_extension_tried:
	dc.b	0
	.even

	.bss
mxdrv_name:
	ds.b	256
mdx_name:
	ds.b	256
pdx_name:
	ds.b	PDX_NAME_CAPACITY
mdx_buffer:
	ds.b	MDX_CAPACITY+8

	.end	start
