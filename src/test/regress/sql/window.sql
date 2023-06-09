--
-- WINDOW FUNCTIONS
--

-- Enforce use of COMMIT instead of 2PC for temporary objects

CREATE TEMPORARY TABLE empsalary (
    depname varchar,
    empno bigint,
    salary int,
    enroll_date date
);

INSERT INTO empsalary VALUES
('develop', 10, 5200, '2007-08-01'),
('sales', 1, 5000, '2006-10-01'),
('personnel', 5, 3500, '2007-12-10'),
('sales', 4, 4800, '2007-08-08'),
('personnel', 2, 3900, '2006-12-23'),
('develop', 7, 4200, '2008-01-01'),
('develop', 9, 4500, '2008-01-01'),
('sales', 3, 4800, '2007-08-01'),
('develop', 8, 6000, '2006-10-01'),
('develop', 11, 5200, '2007-08-15');

SELECT depname, empno, salary, sum(salary) OVER (PARTITION BY depname) FROM empsalary ORDER BY 1, 2, 3, 4;

SELECT depname, empno, salary, rank() OVER (PARTITION BY depname ORDER BY salary) FROM empsalary ORDER BY 1, 2, 3, 4;

-- with GROUP BY
SELECT four, ten, SUM(SUM(four)) OVER (PARTITION BY four), AVG(ten) FROM tenk1
GROUP BY four, ten ORDER BY four, ten;

SELECT depname, empno, salary, sum(salary) OVER w FROM empsalary WINDOW w AS (PARTITION BY depname) ORDER BY empno,salary;

SELECT depname, empno, salary, rank() OVER w FROM empsalary WINDOW w AS (PARTITION BY depname ORDER BY salary) ORDER BY rank() OVER w,empno;

-- empty window specification
SELECT COUNT(*) OVER () FROM tenk1 WHERE unique2 < 10 ORDER BY 1;

SELECT COUNT(*) OVER w FROM tenk1 WHERE unique2 < 10 WINDOW w AS () ORDER BY 1;

-- no window operation
SELECT four FROM tenk1 WHERE FALSE WINDOW w AS (PARTITION BY ten);

-- cumulative aggregate
SELECT sum(four) OVER (PARTITION BY ten ORDER BY unique2) AS sum_1, ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT row_number() OVER (ORDER BY unique2) FROM tenk1 WHERE unique2 < 10 ORDER BY 1;

SELECT rank() OVER (PARTITION BY four ORDER BY ten) AS rank_1, ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT dense_rank() OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT percent_rank() OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT cume_dist() OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT ntile(3) OVER (ORDER BY ten, four), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT ntile(NULL) OVER (ORDER BY ten, four), ten, four FROM tenk1 LIMIT 2;

SELECT lag(ten) OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT lag(ten, four) OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT lag(ten, four, 0) OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT lead(ten) OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT lead(ten * 2, 1) OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT lead(ten * 2, 1, -1) OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT first_value(ten) OVER (PARTITION BY four ORDER BY ten), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

-- last_value returns the last row of the frame, which is CURRENT ROW in ORDER BY window.
SELECT last_value(four) OVER (ORDER BY ten, four), ten, four FROM tenk1 WHERE unique2 < 10 ORDER BY 1, 2, 3;

SELECT last_value(ten) OVER (PARTITION BY four), ten, four FROM
	(SELECT * FROM tenk1 WHERE unique2 < 10 ORDER BY four, ten)s
	ORDER BY four, ten;

SELECT nth_value(ten, four + 1) OVER (PARTITION BY four), ten, four
	FROM (SELECT * FROM tenk1 WHERE unique2 < 10 ORDER BY four, ten)s
	ORDER BY four, ten;

SELECT ten, two, sum(hundred) AS gsum, sum(sum(hundred)) OVER (PARTITION BY two ORDER BY ten) AS wsum 
FROM tenk1 GROUP BY ten, two
ORDER BY ten, two;

SELECT count(*) OVER (PARTITION BY four), four FROM (SELECT * FROM tenk1 WHERE two = 1)s WHERE unique2 < 10 ORDER BY 1, 2;

SELECT (count(*) OVER (PARTITION BY four ORDER BY ten) + 
  sum(hundred) OVER (PARTITION BY four ORDER BY ten))::varchar AS cntsum 
  FROM tenk1 WHERE unique2 < 10 ORDER BY 1;

-- opexpr with different windows evaluation.
SELECT * FROM(
  SELECT count(*) OVER (PARTITION BY four ORDER BY ten) +
    sum(hundred) OVER (PARTITION BY two ORDER BY ten) AS total,
    count(*) OVER (PARTITION BY four ORDER BY ten) AS fourcount,
    sum(hundred) OVER (PARTITION BY two ORDER BY ten) AS twosum
    FROM tenk1
)sub
WHERE total <> fourcount + twosum;

SELECT avg(four) OVER (PARTITION BY four ORDER BY thousand / 100) FROM tenk1 WHERE unique2 < 10 ORDER BY 1;

SELECT ten, two, sum(hundred) AS gsum, sum(sum(hundred)) OVER win AS wsum 
FROM tenk1 GROUP BY ten, two WINDOW win AS (PARTITION BY two ORDER BY ten) ORDER BY 1, 2, 3, 4;

-- more than one window with GROUP BY
SELECT sum(salary),
	row_number() OVER (ORDER BY depname),
	sum(sum(salary)) OVER (ORDER BY depname DESC)
FROM empsalary GROUP BY depname;

-- identical windows with different names
SELECT sum(salary) OVER w1, count(*) OVER w2
FROM empsalary WINDOW w1 AS (ORDER BY salary), w2 AS (ORDER BY salary) ORDER BY 1, 2;

-- subplan
SELECT lead(ten, (SELECT two FROM tenk1 WHERE s.unique2 = unique2)) OVER (PARTITION BY four ORDER BY ten)
FROM tenk1 s WHERE unique2 < 10 ORDER BY 1;

-- empty table
SELECT count(*) OVER (PARTITION BY four) FROM (SELECT * FROM tenk1 WHERE FALSE)s;

-- mixture of agg/wfunc in the same window
SELECT sum(salary) OVER w, rank() OVER w FROM empsalary WINDOW w AS (PARTITION BY depname ORDER BY salary DESC) ORDER BY 1, 2;

-- strict aggs
SELECT empno, depname, salary, bonus, depadj, MIN(bonus) OVER (ORDER BY empno), MAX(depadj) OVER () FROM(
	SELECT *,
		CASE WHEN enroll_date < '2008-01-01' THEN 2008 - extract(YEAR FROM enroll_date) END * 500 AS bonus,
		CASE WHEN
			AVG(salary) OVER (PARTITION BY depname) < salary
		THEN 200 END AS depadj FROM empsalary
)s ORDER BY empno;

-- window function over ungrouped agg over empty row set (bug before 9.1)
SELECT SUM(COUNT(f1)) OVER () FROM int4_tbl WHERE f1=42;

-- window function with ORDER BY an expression involving aggregates (9.1 bug)
select ten,
  sum(unique1) + sum(unique2) as res,
  rank() over (order by sum(unique1) + sum(unique2)) as rank
from tenk1
group by ten order by ten;

-- window and aggregate with GROUP BY expression (9.2 bug)
explain (costs off)
select first_value(max(x)) over (), y
  from (select unique1 as x, ten+four as y from tenk1) ss
  group by y;

-- test non-default frame specifications
SELECT four, ten,
	sum(ten) over (partition by four order by ten),
	last_value(ten) over (partition by four order by ten)
FROM (select distinct ten, four from tenk1) ss ORDER BY 1, 2, 3, 4;

SELECT four, ten,
	sum(ten) over (partition by four order by ten range between unbounded preceding and current row),
	last_value(ten) over (partition by four order by ten range between unbounded preceding and current row)
FROM (select distinct ten, four from tenk1) ss ORDER BY 1, 2, 3, 4;

SELECT four, ten,
	sum(ten) over (partition by four order by ten range between unbounded preceding and unbounded following),
	last_value(ten) over (partition by four order by ten range between unbounded preceding and unbounded following)
FROM (select distinct ten, four from tenk1) ss ORDER BY 1, 2, 3, 4;

SELECT four, ten/4 as two,
	sum(ten/4) over (partition by four order by ten/4 range between unbounded preceding and current row),
	last_value(ten/4) over (partition by four order by ten/4 range between unbounded preceding and current row)
FROM (select distinct ten, four from tenk1) ss ORDER BY 1, 2, 3, 4;

SELECT four, ten/4 as two,
	sum(ten/4) over (partition by four order by ten/4 rows between unbounded preceding and current row),
	last_value(ten/4) over (partition by four order by ten/4 rows between unbounded preceding and current row)
FROM (select distinct ten, four from tenk1) ss ORDER BY 1, 2, 3, 4;

SELECT sum(unique1) over (order by four, unique1 range between current row and unbounded following),
	unique1, four
FROM tenk1 WHERE unique1 < 10;

SELECT sum(unique1) over (rows between current row and unbounded following),
	unique1, four
FROM (SELECT unique1, four FROM tenk1 WHERE unique1 < 10 ORDER BY 1, 2) stenk1 ORDER BY 1, 2, 3;

SELECT sum(unique1) over (rows between 2 preceding and 2 following),
	unique1, four
FROM (SELECT unique1, four FROM tenk1 WHERE unique1 < 10 ORDER BY 1, 2) stenk1 ORDER BY 1, 2, 3;

SELECT sum(unique1) over (rows between 2 preceding and 1 preceding),
	unique1, four
FROM (SELECT unique1, four FROM tenk1 WHERE unique1 < 10 ORDER BY 1, 2) stenk1 ORDER BY 1, 2, 3;

SELECT sum(unique1) over (rows between 1 following and 3 following),
	unique1, four
FROM (SELECT unique1, four FROM tenk1 WHERE unique1 < 10 ORDER BY 1, 2) stenk1 ORDER BY 1, 2, 3;

SELECT sum(unique1) over (rows between unbounded preceding and 1 following),
	unique1, four
FROM (SELECT unique1, four FROM tenk1 WHERE unique1 < 10 ORDER BY 1, 2) stenk1 ORDER BY 1, 2, 3;

SELECT sum(unique1) over (w range between current row and unbounded following),
	unique1, four
FROM tenk1 WHERE unique1 < 10 WINDOW w AS (order by four, unique1);

-- fail: not implemented yet
SELECT sum(unique1) over (order by four, ten, unique1 range between 2::int8 preceding and 1::int2 preceding),
	unique1, four
FROM tenk1 WHERE unique1 < 10;

SELECT first_value(unique1) over w,
	nth_value(unique1, 2) over w AS nth_2,
	last_value(unique1) over w, unique1, four
FROM tenk1 WHERE unique1 < 10
WINDOW w AS (order by four, ten, unique1 range between current row and unbounded following);

SELECT sum(unique1) over
	(order by unique1
	 rows (SELECT unique1 FROM tenk1 ORDER BY unique1 LIMIT 1) + 1 PRECEDING),
	unique1
FROM (SELECT unique1, four FROM tenk1 WHERE unique1 < 10 ORDER BY 1, 2) stenk1;

CREATE TEMP VIEW v_window AS
	SELECT i, sum(i) over (order by i rows between 1 preceding and 1 following) as sum_rows
	FROM generate_series(1, 10) i;

SELECT * FROM v_window;

SELECT pg_get_viewdef('v_window');

-- with UNION
SELECT count(*) OVER (PARTITION BY four) FROM (SELECT * FROM tenk1 UNION ALL SELECT * FROM tenk2)s LIMIT 0;

-- ordering by a non-integer constant is allowed
SELECT rank() OVER (ORDER BY length('abc'));

-- can't order by another window function
SELECT rank() OVER (ORDER BY rank() OVER (ORDER BY random()));

-- some other errors
SELECT * FROM empsalary WHERE row_number() OVER (ORDER BY salary) < 10;

SELECT * FROM empsalary INNER JOIN tenk1 ON row_number() OVER (ORDER BY salary) < 10;

SELECT rank() OVER (ORDER BY 1), count(*) FROM empsalary GROUP BY 1;

SELECT * FROM rank() OVER (ORDER BY random());

DELETE FROM empsalary WHERE (rank() OVER (ORDER BY random())) > 10;

DELETE FROM empsalary RETURNING rank() OVER (ORDER BY random());

SELECT count(*) OVER w FROM tenk1 WINDOW w AS (ORDER BY unique1), w AS (ORDER BY unique1);

SELECT rank() OVER (PARTITION BY four, ORDER BY ten) FROM tenk1;

SELECT count() OVER () FROM tenk1;

SELECT generate_series(1, 100) OVER () FROM empsalary;

SELECT ntile(0) OVER (ORDER BY ten), ten, four FROM tenk1;

SELECT nth_value(four, 0) OVER (ORDER BY ten), ten, four FROM tenk1;

-- cleanup
DROP TABLE empsalary;

CREATE TABLE DISTRIBUTE_WINDOW_T1(A INT, B INT, C INT, D INT);
INSERT INTO DISTRIBUTE_WINDOW_T1 VALUES (GENERATE_SERIES(1, 2), GENERATE_SERIES(1, 3), GENERATE_SERIES(1, 5), GENERATE_SERIES(1, 7));

EXPLAIN (COSTS OFF, VERBOSE ON)
SELECT 
RANK() OVER (PARTITION BY A ORDER BY B, C, D), 
A, B, C, D,
SUM(A) OVER (PARTITION BY A ORDER BY B, C, D), 
SUM(B) OVER (PARTITION BY B ORDER BY A, C, D),
SUM(C) OVER (PARTITION BY C ORDER BY A, B, D),
SUM(D) OVER (PARTITION BY D ORDER BY A, B, C)
FROM DISTRIBUTE_WINDOW_T1 ORDER BY 1, 2, 3, 4, 5;

SELECT 
RANK() OVER (PARTITION BY A ORDER BY B, C, D), 
A, B, C, D,
SUM(A) OVER (PARTITION BY A ORDER BY B, C, D), 
SUM(B) OVER (PARTITION BY B ORDER BY A, C, D),
SUM(C) OVER (PARTITION BY C ORDER BY A, B, D),
SUM(D) OVER (PARTITION BY D ORDER BY A, B, C)
FROM DISTRIBUTE_WINDOW_T1 ORDER BY 1, 2, 3, 4, 5;

DROP TABLE DISTRIBUTE_WINDOW_T1;

CREATE TABLE DISTRIBUTE_WINDOW_T1(ID INT ,NAME VARCHAR(20),ZIP VARCHAR(20));
CREATE TABLE DISTRIBUTE_WINDOW_T2(ID INT,STREET VARCHAR(20),ZIP VARCHAR(20),C_D_ID INT,C_ID INT) ;
CREATE TABLE DISTRIBUTE_WINDOW_T3(C_ID INT,STREET VARCHAR(20),ZIP VARCHAR(20) ,C_D_ID INT,ID INT) ;
CREATE TABLE DISTRIBUTE_WINDOW_T4 (ID INTEGER,STREET VARCHAR(20),ZIP CHAR(9),C_D_ID INTEGER,C_W_ID INTEGER);

EXPLAIN (COSTS OFF, VERBOSE ON)
SELECT MAX(DT1.ID) OVER W 
FROM
(
	SELECT DISTRIBUTE_WINDOW_T1.ID, DISTRIBUTE_WINDOW_T1.NAME, DISTRIBUTE_WINDOW_T1.ZIP
	FROM DISTRIBUTE_WINDOW_T1
	WHERE (DISTRIBUTE_WINDOW_T1.ID IN ( SELECT A.ID FROM DISTRIBUTE_WINDOW_T4 A WHERE NOT (EXISTS ( SELECT B.C_ID, B.STREET, B.ZIP, B.C_D_ID, B.ID FROM DISTRIBUTE_WINDOW_T2 B WHERE B.C_ID = A.ID)))) ORDER BY DISTRIBUTE_WINDOW_T1.ID, DISTRIBUTE_WINDOW_T1.NAME, DISTRIBUTE_WINDOW_T1.ZIP LIMIT 7
) DT1 LEFT JOIN 
(
	SELECT A.ID, A.STREET, A.ZIP, A.C_D_ID, A.C_ID 
	FROM DISTRIBUTE_WINDOW_T3 A 
	RIGHT JOIN DISTRIBUTE_WINDOW_T4 B ON A.ID = B.ID::NUMERIC AND A.C_D_ID = B.C_D_ID  
	WHERE A.ID IS NOT NULL
)EXPLICIT_JOIN_02 ON DT1.ID=EXPLICIT_JOIN_02.ID 
WHERE DT1.ID>=1 
WINDOW W AS (PARTITION BY DT1.ZIP ORDER BY DT1.ID DESC)
ORDER BY 1;

SELECT MAX(DT1.ID) OVER W 
FROM
(
	SELECT DISTRIBUTE_WINDOW_T1.ID, DISTRIBUTE_WINDOW_T1.NAME, DISTRIBUTE_WINDOW_T1.ZIP
	FROM DISTRIBUTE_WINDOW_T1
	WHERE (DISTRIBUTE_WINDOW_T1.ID IN ( SELECT A.ID FROM DISTRIBUTE_WINDOW_T4 A WHERE NOT (EXISTS ( SELECT B.C_ID, B.STREET, B.ZIP, B.C_D_ID, B.ID FROM DISTRIBUTE_WINDOW_T2 B WHERE B.C_ID = A.ID)))) ORDER BY DISTRIBUTE_WINDOW_T1.ID, DISTRIBUTE_WINDOW_T1.NAME, DISTRIBUTE_WINDOW_T1.ZIP LIMIT 7
) DT1 LEFT JOIN 
(
	SELECT A.ID, A.STREET, A.ZIP, A.C_D_ID, A.C_ID 
	FROM DISTRIBUTE_WINDOW_T3 A 
	RIGHT JOIN DISTRIBUTE_WINDOW_T4 B ON A.ID = B.ID::NUMERIC AND A.C_D_ID = B.C_D_ID  
	WHERE A.ID IS NOT NULL
)EXPLICIT_JOIN_02 ON DT1.ID=EXPLICIT_JOIN_02.ID 
WHERE DT1.ID>=1 
WINDOW W AS (PARTITION BY DT1.ZIP ORDER BY DT1.ID DESC)
ORDER BY 1;

DROP TABLE DISTRIBUTE_WINDOW_T1;
DROP TABLE DISTRIBUTE_WINDOW_T2;
DROP TABLE DISTRIBUTE_WINDOW_T3;
DROP TABLE DISTRIBUTE_WINDOW_T4;

CREATE FUNCTION nth_value_def(val anyelement, n integer = 1) RETURNS anyelement
  LANGUAGE internal WINDOW IMMUTABLE STRICT AS 'window_nth_value';

SELECT nth_value_def(ten) OVER (PARTITION BY four order by unique1) as No1, *
  FROM (SELECT * FROM tenk1 WHERE unique2 < 10 ORDER BY four, ten) s order by No1;

drop table bmsql_item;
create table bmsql_item ( i_price numeric(5,2),i_data varchar(50));
insert into bmsql_item values('1.0',1);
select
    first_value(avg(alias7.alias4)) over (order by 1) alias12,
    length(alias7.alias5) alias14
from (select
            bmsql_item.i_price alias4,
            cast(bmsql_item.i_data as varchar(100))as alias5
        from
            bmsql_item
    ) alias7
group by
    alias7.alias5;
drop table bmsql_item;
