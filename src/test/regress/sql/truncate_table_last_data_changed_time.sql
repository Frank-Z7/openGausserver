create table pg_stat_t1(aa int, bb int);
create global temp table pg_stat_gtt1(aa int, bb int);
create table pg_stat_p1(aa int, bb int) partition by range(aa)
(
    partition p1 values less than(10000), 
    partition p2 values less than(20000), 
    partition p3 values less than(maxvalue)
);
create table pg_stat_p2(aa int, bb int) partition by range(aa)
(
    partition p1 values less than(10000), 
    partition p2 values less than(20000), 
    partition p3 values less than(maxvalue)
);
CREATE TABLE pg_stat_range_range_p3
(
    product_id     INT4 NOT NULL,
    customer_id    INT4 PRIMARY KEY,
    time_id        DATE,
    channel_id     CHAR(1),
    type_id        INT4,
    quantity_sold  NUMERIC(3),
    amount_sold    NUMERIC(10,2)
)
PARTITION BY RANGE (customer_id) SUBPARTITION BY RANGE (time_id)
(
    PARTITION customer1 VALUES LESS THAN (200)
    (
        SUBPARTITION customer1_2008 VALUES LESS THAN ('2009-01-01'),
        SUBPARTITION customer1_2009 VALUES LESS THAN ('2010-01-01'),
        SUBPARTITION customer1_2010 VALUES LESS THAN ('2011-01-01'),
        SUBPARTITION customer1_2011 VALUES LESS THAN ('2012-01-01')
    ),
    PARTITION customer2 VALUES LESS THAN (500)
    (
        SUBPARTITION customer2_2008 VALUES LESS THAN ('2009-01-01'),
        SUBPARTITION customer2_2009 VALUES LESS THAN ('2010-01-01'),
        SUBPARTITION customer2_2010 VALUES LESS THAN ('2011-01-01'),
        SUBPARTITION customer2_2011 VALUES LESS THAN ('2012-01-01')
    ),
    PARTITION customer3 VALUES LESS THAN (800),
    PARTITION customer4 VALUES LESS THAN (1200)
    (
        SUBPARTITION customer4_all VALUES LESS THAN ('2012-01-01')
    )
);
CREATE TABLE pg_stat_range_range_p4
(
    product_id     INT4 NOT NULL,
    customer_id    INT4 PRIMARY KEY,
    time_id        DATE,
    channel_id     CHAR(1),
    type_id        INT4,
    quantity_sold  NUMERIC(3),
    amount_sold    NUMERIC(10,2)
)
PARTITION BY RANGE (customer_id) SUBPARTITION BY RANGE (time_id)
(
    PARTITION customer1 VALUES LESS THAN (200)
    (
        SUBPARTITION customer1_2008 VALUES LESS THAN ('2009-01-01'),
        SUBPARTITION customer1_2009 VALUES LESS THAN ('2010-01-01'),
        SUBPARTITION customer1_2010 VALUES LESS THAN ('2011-01-01'),
        SUBPARTITION customer1_2011 VALUES LESS THAN ('2012-01-01')
    ),
    PARTITION customer2 VALUES LESS THAN (500)
    (
        SUBPARTITION customer2_2008 VALUES LESS THAN ('2009-01-01'),
        SUBPARTITION customer2_2009 VALUES LESS THAN ('2010-01-01'),
        SUBPARTITION customer2_2010 VALUES LESS THAN ('2011-01-01'),
        SUBPARTITION customer2_2011 VALUES LESS THAN ('2012-01-01')
    ),
    PARTITION customer3 VALUES LESS THAN (800),
    PARTITION customer4 VALUES LESS THAN (1200)
    (
        SUBPARTITION customer4_all VALUES LESS THAN ('2012-01-01')
    )
);

select relname, pg_stat_get_last_data_changed_time(oid) is null
from pg_class
where relname='pg_stat_t1' or relname='pg_stat_gtt1' or relname='pg_stat_p1' or relname='pg_stat_p2'
    or relname='pg_stat_range_range_p3' or relname='pg_stat_range_range_p4'
order by relname;

truncate table pg_stat_t1, pg_stat_gtt1;
alter table pg_stat_p1 truncate partition p1;
truncate table pg_stat_p2;
alter table pg_stat_range_range_p3 truncate partition customer1;
alter table pg_stat_range_range_p4 truncate subpartition customer1_2008;

SELECT pg_sleep(5);

select relname, pg_stat_get_last_data_changed_time(oid) is null
from pg_class
where relname='pg_stat_t1' or relname='pg_stat_gtt1' or relname='pg_stat_p1' or relname='pg_stat_p2'
    or relname='pg_stat_range_range_p3' or relname='pg_stat_range_range_p4'
order by relname;

drop table pg_stat_t1;
drop table pg_stat_gtt1;
drop table pg_stat_p1;
drop table pg_stat_p2;
drop table pg_stat_range_range_p3;
drop table pg_stat_range_range_p4;