--prepare
DROP SCHEMA segment_subpartition_truncate CASCADE;
CREATE SCHEMA segment_subpartition_truncate;
SET CURRENT_SCHEMA TO segment_subpartition_truncate;

--truncate partition/subpartition
CREATE TABLE list_list
(
    month_code VARCHAR2 ( 30 ) NOT NULL ,
    dept_code  VARCHAR2 ( 30 ) NOT NULL ,
    user_no    VARCHAR2 ( 30 ) NOT NULL ,
    sales_amt  int
) WITH (SEGMENT=ON)
PARTITION BY LIST (month_code) SUBPARTITION BY LIST (dept_code)
(
  PARTITION p_201901 VALUES ( '201902' )
  (
    SUBPARTITION p_201901_a VALUES ( '1' ),
    SUBPARTITION p_201901_b VALUES ( default )
  ),
  PARTITION p_201902 VALUES ( '201903' )
  (
    SUBPARTITION p_201902_a VALUES ( '1' ),
    SUBPARTITION p_201902_b VALUES ( '2' )
  )
);
insert into list_list values('201902', '1', '1', 1);
insert into list_list values('201902', '2', '1', 1);
insert into list_list values('201902', '1', '1', 1);
insert into list_list values('201903', '2', '1', 1);
insert into list_list values('201903', '1', '1', 1);
insert into list_list values('201903', '2', '1', 1);
select * from list_list;

select * from list_list partition (p_201901);
alter table list_list truncate partition p_201901;
select * from list_list partition (p_201901);

select * from list_list partition (p_201902);
alter table list_list truncate partition p_201902;
select * from list_list partition (p_201902);
select * from list_list;

insert into list_list values('201902', '1', '1', 1);
insert into list_list values('201902', '2', '1', 1);
insert into list_list values('201902', '1', '1', 1);
insert into list_list values('201903', '2', '1', 1);
insert into list_list values('201903', '1', '1', 1);
insert into list_list values('201903', '2', '1', 1);

select * from list_list subpartition (p_201901_a);
alter table list_list truncate subpartition p_201901_a;
select * from list_list subpartition (p_201901_a);

select * from list_list subpartition (p_201901_b);
alter table list_list truncate subpartition p_201901_b;
select * from list_list subpartition (p_201901_b);

select * from list_list subpartition (p_201902_a);
alter table list_list truncate subpartition p_201902_a;
select * from list_list subpartition (p_201902_a);

select * from list_list subpartition (p_201902_b);
alter table list_list truncate subpartition p_201902_b;
select * from list_list subpartition (p_201902_b);

select * from list_list;