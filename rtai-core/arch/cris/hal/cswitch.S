	;; $r10 is a pointer to a pointer to a RT_TASK
	;; $r11 is pointer to a RT_TASK
	;; r10 and r11 are filled with arguments the first time
	;; the task gets to run.
	.globl up_task_sw
up_task_sw:
	push	$srp
	subq	10*4, $sp
	movem	$r9, [$sp]
	subq	2*4, $sp

	;move.d	[$r10], $r0
	;move.d	$sp, [$r0]
	move.d	$sp, [[$r10]]
	
	;; goodbye old, hello new
	
	move.d	$r11, [$r10]	; rt_current = next
	move.d	[$r11], $sp
		
	movem	[$sp+], $r11
	jump	[$sp+]
