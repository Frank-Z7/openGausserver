create schema vector_name;
set current_schema=vector_name;
create table vector_name.timetz_003
(
timetz_decimal DECIMAL,
timetz_interval interval,
timetz_numeric numeric)
with (ORIENTATION = column);

create table vector_name.timetz_001
(
timetz_decimal DECIMAL,
timetz_interval interval,
timetz_numeric numeric)
with (ORIENTATION = column);

create table vector_name.int_004
(c_int int,
int_bigint bigint,
int_smallint SMALLINT,
int_oid oid,
int_bool bool,
int_char char(5),
int_date date,
int_abstime abstime,
int_reltime reltime)
with (ORIENTATION = column);

insert into timetz_003 values(2147483646,2147483646,2147483646);
insert into int_004 values (2147483646,2147483646,32767,20,'t',-99,'2010-10-10','2010-10-10','90');
insert into timetz_001 values(2147483646,36,2147483646);

select int_bigint::name from int_004 order by int_bigint;

select t1.int_bigint,t3.timetz_decimal::name from int_004 t1    left join int_004 t2 on t1.int_bigint::name =t2.c_int::name       join timetz_001  t3 on t2.c_int::name =t3.timetz_decimal::name  order by 1, 2;

select t1.int_bigint,t3.timetz_decimal::name from int_004 t1    left join int_004 t2 on t1.int_bigint::name =t2.c_int::name       join timetz_001  t3 on t2.c_int::name =t3.timetz_decimal::name;

drop schema vector_name cascade;