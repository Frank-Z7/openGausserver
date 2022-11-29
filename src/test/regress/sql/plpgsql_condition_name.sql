create or replace procedure plpgsql_condition_name1()
as
begin
raise cannot_modify_xidbase using errcode = "XX013";
end;
/

create or replace procedure plpgsql_condition_name1()
as
begin
raise invalid_audit_log using errcode = "SE001";
end;
/

create or replace procedure plpgsql_condition_name1()
as
begin
raise invalid_temp_objects using errcode = "42P18";
end;
/

create or replace procedure plpgsql_condition_name1()
as
begin
raise SLOW_QUERY_STATEMENT_NAME using errcode = "26001";
end;
/

drop procedure plpgsql_condition_name1();
