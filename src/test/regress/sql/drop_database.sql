create database test_db;
\c test_db
drop server mot_server;
create foreign data wrapper test_fdw;
create server test_server foreign data wrapper test_fdw;
create foreign table test_table(a int) server test_server;
\c regression
drop database test_db;