@r@
position p;
expression e1;
identifier lockFn =~ "^(spin|read|write|rcu_read|rcu_write|mutex)_(try)?lock((_bh|_irq|_irqsave|_nested|_interruptible|_killable|_nest_lock)?(_nested|_sched)?)?$";
@@

(
lockFn@p(e1,...)
|
lockFn@p()
)

@script:ocaml s@
p << r.p;
fn;
@@

fn := make_ident ((List.hd p).current_element)

@@
position r.p;
identifier s.fn;
expression r.e1;
identifier r.lockFn;
@@

(
lockFn@p(e1,...);
+ log_lock(1,MK_STRING(e1),e1,__FILE__,__LINE__,MK_STRING(fn),MK_STRING(lockFn));
|
lockFn@p();
+ log_lock(1,"static",(void*)0x42,__FILE__,__LINE__,MK_STRING(fn),MK_STRING(lockFn));
)

@t@
position p;
expression e1;
identifier unlockFn =~ "^(spin|read|write|rcu_read|rcu_write|mutex)_unlock((_bh|_irq|_irqrestore|_nested|_interruptible|_killable|_nest_lock)?(_nested|_sched)?)?$";
@@

(
unlockFn@p(e1,...)
|
unlockFn@p()
)

@script:ocaml u@
p << t.p;
fn;
@@

fn := make_ident ((List.hd p).current_element)

@@
position t.p;
identifier u.fn;
expression t.e1;
identifier t.unlockFn;
@@

(
unlockFn@p(e1,...);
+ log_lock(0,MK_STRING(e1),e1,__FILE__,__LINE__,MK_STRING(fn),MK_STRING(unlockFn));
|
unlockFn@p();
+ log_lock(0,"static",(void*)0x42,__FILE__,__LINE__,MK_STRING(fn),MK_STRING(unlockFn));
)

