std::string createSQL_serverInfo = R"(
	CREATE TABLE IF NOT EXISTS server_info (
	server_id int unsigned not null primary key auto_increment,
	master_sid int unsigned not null default 0,   -- if master_id is zero, self is master; else self is slave.
	host varchar(255) not null,
	port int unsigned not null default 3306,
	user varchar(32) not null default '',
	passwd varchar(64) not null default '',
	timeout int unsigned not null default 0,
	default_database_name varchar(255) not null default '',
	index(master_sid),
	unique(host, port)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;)";


std::string createSQL_tableInfo = R"(
	CREATE TABLE IF NOT EXISTS table_info (
	id int unsigned not null primary key auto_increment,  -- just using for stepwise loading
	table_name varchar(64) not null unique,
	split_type tinyint not null,	-- 0: mod type, 1: range type.
	range_span int not null default -1,
	database_category varchar(64) not null default '',  -- only for range type.
	secondary_split tinyint not null default 0, -- only for range type. 格式：tablename#: table_name0, table_name1, table_name2, ...
	secondary_split_span int unsigned not null default 0, -- only for range type.
	table_count int unsigned not null default 0,   -- only for mod type. 0 & 1 means no splitted.
	hint_field varchar(64) not null default '',  -- 分库分表字段
	unique (table_name)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;)";


std::string createSQL_splitTableInfo = R"(
	CREATE TABLE IF NOT EXISTS split_table_info (
	id int unsigned not null primary key auto_increment,  -- just using for stepwise loading
	table_name varchar(64) not null default '',
	table_number int not null,    -- 分表编号，从0开始。如果一个表没有分表，该字段忽略，但建议设置为0。
	server_id int unsigned not null default 0,    -- MUST fill the master database id.
	database_name varchar(64) not null,
	unique (table_name, table_number)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;)";


std::string createSQL_splitRangeInfo = R"(
	CREATE TABLE IF NOT EXISTS split_range_info (
	id int unsigned not null primary key auto_increment,  -- just using for stepwise loading
	database_category varchar(64) not null,
	split_index int unsigned not null, -- from 0.
	index_type int not null default 0, -- 0: normal, 1: odd sub db; 2: even sub db.
	database_name varchar(64) not null,
	server_id int unsigned not null default 0 ,   -- MUST fill the master database id.
	unique (database_category, split_index, index_type)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;)";


std::string createSQL_variableSetting = R"(
	CREATE TABLE IF NOT EXISTS variable_setting (
	id int unsigned not null primary key auto_increment, 
	name varchar(255) not null unique default '',
	value varchar(255) not null default '',
	mtime timestamp not null default CURRENT_TIMESTAMP on update CURRENT_TIMESTAMP,
	index (name)
)ENGINE=InnoDB DEFAULT CHARSET=utf8;)";



std::string initSQLs = R"(

INSERT INTO variable_setting (name) VALUES ("DBProxy config data update");
INSERT INTO variable_setting (name, value) VALUES ("secondary split number base", "0");
INSERT INTO variable_setting (name, value) VALUES ("default split range span", "200000");
INSERT INTO variable_setting (name, value) VALUES ("DBProxy config table structure version", "2");

)";

