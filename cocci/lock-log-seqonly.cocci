@r@
position p;
expression arg;
identifier lockFn =~ "(__)?(raw_)?(raw|read|write)_seq(lock|count|begin)(_begin|_nested|_bh|_irq_excl|_or_lock|_excl_bh|_excl_irq)?$";
expression foo;
@@

(
foo = lockFn@p(arg,...);
+ log_lock(1,MK_STRING(arg),arg,__FILE__,__LINE__,__func__,MK_STRING(lockFn));
|
lockFn@p(arg,...);
+ log_lock(1,MK_STRING(arg),arg,__FILE__,__LINE__,__func__,MK_STRING(lockFn));
)


@s@
position p;
expression arg ;
identifier unlockFn =~ "(__)?read_seq(count_)?retry$";
identifier l;
statement s1;
@@

(
if (unlockFn@p(arg,...)) {
+ log_lock(0,MK_STRING(arg),arg,__FILE__,__LINE__,__func__,MK_STRING(unlockFn));
... when any 
(
goto l;
|
return ...;
)
} else s1
+ log_lock(0,MK_STRING(arg),arg,__FILE__,__LINE__,__func__,MK_STRING(unlockFn));
|
if (unlockFn@p(arg,...))
goto retry;
+ log_lock(0,MK_STRING(arg),arg,__FILE__,__LINE__,__func__,MK_STRING(unlockFn));
|
unlockFn@p(arg,...);
+ log_lock(0,MK_STRING(arg),arg,__FILE__,__LINE__,__func__,MK_STRING(unlockFn));
|
 while (unlockFn@p(arg,...)) s1
+ log_lock(0,MK_STRING(arg),arg,__FILE__,__LINE__,__func__,MK_STRING(unlockFn));
)

@t@
position p;
expression arg;
identifier unlockFn =~ "^(read|write)_seq(unlock|count|end)(_end|_nested|_bh|_irq_excl|_or_lock|_excl_bh|_excl_irq|_irqrestore|_excl_irqrestore)?$";
@@


(
unlockFn@p(arg,...);
+ log_lock(0,MK_STRING(arg),arg,__FILE__,__LINE__,__func__,MK_STRING(unlockFn));
)

