\ Words implementing frame drawing
\ XXX Filled boxes are left as an exercise for the reader... ;-/
\ $FreeBSD$

marker task-frames.4th

variable h_el
variable v_el
variable lt_el
variable lb_el
variable rt_el
variable rb_el
variable fill

\ ASCII frames (used when serial console is detected)
 45 constant ascii_dash
124 constant ascii_pipe
 43 constant ascii_plus

s" arch-pc98" environment? [if]
	\ Single frames
	149 constant sh_el
	150 constant sv_el
	152 constant slt_el
	154 constant slb_el
	153 constant srt_el
	155 constant srb_el
	\ Double frames
	149 constant dh_el
	150 constant dv_el
	152 constant dlt_el
	154 constant dlb_el
	153 constant drt_el
	155 constant drb_el
	\ Fillings
	0 constant fill_none
	32 constant fill_blank
	135 constant fill_dark
	135 constant fill_med
	135 constant fill_bright
[else]
	\ Single frames
	196 constant sh_el
	179 constant sv_el
	218 constant slt_el
	192 constant slb_el
	191 constant srt_el
	217 constant srb_el
	\ Double frames
	205 constant dh_el
	186 constant dv_el
	201 constant dlt_el
	200 constant dlb_el
	187 constant drt_el
	188 constant drb_el
	\ Fillings
	0 constant fill_none
	32 constant fill_blank
	176 constant fill_dark
	177 constant fill_med
	178 constant fill_bright
[then]

: hline	( len x y -- )	\ Draw horizontal single line
	at-xy		\ move cursor
	0 do
		h_el @ emit
	loop
;

: f_ascii ( -- )	( -- )	\ set frames to ascii
	ascii_dash h_el !
	ascii_pipe v_el !
	ascii_plus lt_el !
	ascii_plus lb_el !
	ascii_plus rt_el !
	ascii_plus rb_el !
;

: f_single	( -- )	\ set frames to single
	boot_serial? if f_ascii exit then
	sh_el h_el !
	sv_el v_el !
	slt_el lt_el !
	slb_el lb_el !
	srt_el rt_el !
	srb_el rb_el !
;

: f_double	( -- )	\ set frames to double
	boot_serial? if f_ascii exit then
	dh_el h_el !
	dv_el v_el !
	dlt_el lt_el !
	dlb_el lb_el !
	drt_el rt_el !
	drb_el rb_el !
;

: vline	( len x y -- )	\ Draw vertical single line
	2dup 4 pick
	0 do
		at-xy
		v_el @ emit
		1+
		2dup
	loop
	2drop 2drop drop
;

: box	( w h x y -- )	\ Draw a box
	2dup 1+ 4 pick 1- -rot
	vline		\ Draw left vert line
	2dup 1+ swap 5 pick + swap 4 pick 1- -rot
	vline		\ Draw right vert line
	2dup swap 1+ swap 5 pick 1- -rot
	hline		\ Draw top horiz line
	2dup swap 1+ swap 4 pick + 5 pick 1- -rot
	hline		\ Draw bottom horiz line
	2dup at-xy lt_el @ emit	\ Draw left-top corner
	2dup 4 pick + at-xy lb_el @ emit	\ Draw left bottom corner
	2dup swap 5 pick + swap at-xy rt_el @ emit	\ Draw right top corner
	2 pick + swap 3 pick + swap at-xy rb_el @ emit
	2drop
;

f_single
fill_none fill !
