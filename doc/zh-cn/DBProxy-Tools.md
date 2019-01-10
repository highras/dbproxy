# DBProxy 管理工具

**管理工具目录**

| 管理工具 | 用途 |
|-----|------|
| DBQuery | 简单单 shard 查询。 |
| DBParamsQuery | 复杂查询。 |
| DBRefresher | 强制 DBProxy 立刻加载更新后的配置库。 |
| DBTableChecker | 检查新建数据表的配置和可达情况。 |
| DBTableStrictChecker | 检查新建数据表、或修改后的数据表的配置和可达情况。 |
| DBTransaction | 事务处理。 |
| DBDeployer | 数据库部署工具。 |


**所有工具空参数运行时，均会出现提示。提示格式为 BNF 范式。**

## DBQuery

使用：

	./DBQuery hint sql cluster
	./DBQuery hint table_name sql cluster
	./DBQuery host port hint sql cluster
	./DBQuery host port hint table_name sql cluster
	./DBQuery host port hintfrom hintto table_name sql cluster

host 默认为 localhost，port 默认为 12321。


例 1：

	$ ./DBQuery 200012 "desc tmp_users" bizCluster01
	Query finished.
	 
	+-------+---------------------+------+-----+-------------------+-------+
	| Field | Type                | Null | Key | Default           | Extra |
	+=======+=====================+======+=====+===================+=======+
	| uid   | int(10) unsigned    | NO   |     |                   |       |
	| gid   | bigint(20) unsigned | NO   |     | 0                 |       |
	| name  | varchar(255)        | NO   |     |                   |       |
	| age   | int(11)             | NO   |     |                   |       |
	| mtime | timestamp           | NO   |     | CURRENT_TIMESTAMP |       |
	+-------+---------------------+------+-----+-------------------+-------+
	5 rows in results.
	 
	time cost 7.161 ms

例 2：

	$ ./DBQuery 200012 "insert into tmp_users (uid, gid, name, age) values (200012, 300, 'first name', 25)" bizCluster02
	Query finished.
	 
	affectedRows: 1
	insertId: 0
	 
	time cost 2.941 ms

例 3：

	$ ./DBQuery 200012 "select * from tmp_users"
	Query finished.
	 
	+--------+-----+-------------+-----+---------------------+
	| uid    | gid | name        | age | mtime               |
	+========+=====+=============+=====+=====================+
	| 200200 | 20  | user 200200 | 24  | 2015-04-07 08:12:56 |
	| 200012 | 300 | first name  | 25  | 2015-04-09 02:32:41 |
	+--------+-----+-------------+-----+---------------------+
	2 rows in results.
	 
	time cost 0.931 ms


## DBParamsQuery

使用：

	./DBParamsQuery [-h host] [-p port] [-t table_name] [-timeout timeout_seconds] [-c cluster] <hintId | -i int_hintIds | -s string_hintIds> sql [param1 param2 ...]

host 默认为 localhost，port 默认为 12321。

例1～3：

	./DBParamsQuery 200101 "select count(*) from fp_user;"

	./DBParamsQuery -i "" "select count(*) from fp_user"

	./DBParamsQuery -i "200101, 101" "select count(*) from fp_user;"


例4：

	$ ./DBParamsQuery -h localhost -p 12322 -t rt_msg 200101 "select ? from ? ?" "uid, other_uid" rt_msg "where mid = 0"
	Query finished.
	 
	+--------+-----------+
	| uid    | other_uid |
	+========+===========+
	| 202323 | 121       |
	+--------+-----------+
	1 rows in results.
	 
	time cost 1.67 ms


例5：

	$ ./DBParamsQuery -p 12322 200101 "select ? from ? ?" "uid, other_uid" rt_msg "where mid = 0"
	Query finished.
	 
	+--------+-----------+
	| uid    | other_uid |
	+========+===========+
	| 202323 | 121       |
	+--------+-----------+
	1 rows in results.
	 
	time cost 1.68 ms


## DBRefresher

用途：强制 DBProxy 检查配置库是否已被更新。如果更新，加载新的配置信息。

使用：

	./DBRefresher host:port ...

可以跟多个 host:port ，以便一次刷新一组，或一个项目的全部 DBProxy。

	./DBProxyRefresher host:port host:port host:port ... 


## DBTableChecker & DBTableStrictChecker

用途：

+ DBTableChecker 会检查每一个分库和分表是否可访问。
+ DBTableStrictChecker 不仅会检查每一个分库和分表是否可访问，还将检查同一个逻辑表的每一个分表字段类型是否一致。

使用：

	./DBTableChecker DBProxy_host DBProxy_port table_name cluster
	./DBTableStrictChecker DBProxy_host DBProxy_port table_name cluster


## DBTransaction

使用：

	./DBTransaction hints tablenames sqls
	./DBTransaction host port hints table_names sqls

host 默认为 localhost，port 默认为 12321。

例：

	./DBTransaction localhost 12322 200003,200001 tmp_users,tmp_users "insert into tmp_users (uid, gid, name, age) values (200019, 29, 'dsd 3', 32); insert into tmp_users (uid, gid, name, age) values (200015, 27.85, 300, 32);"

**注**：该工具仅使用 transaction 接口，不使用 sTransaction 接口。



## DBDeployer

+ 交互式使用：

		./DBDeployer config_db_host config_db_port admin_user admin_pwd

+ 脚本化使用：

		./DBDeployer config_db_host config_db_port admin_user admin_pwd scriptPath

交互模式下，键入 help 可显示全部可用指令（BNF格式）。

**建议使用流程：**

+ 新配置库(需要混淆业务账号)

		create config database <database_name>
		config DBProxy account <user_name> <password> [options]
		config business account <user> <password> [options]
		add master instance <host> <port> [timeout_in_sec]
		show master instances
		add slave instance <host> <port> <master_server_id> [timeout_in_sec]
		grant config database
		add deploy server <master_server_id>
		add hash table <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>
		or
		add hash table from <sql_file_path> <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>
		confuse config database
		update config time

+ 新配置库(无需混淆业务账号)

		create config database <database_name>
		config DBProxy account <user_name> <password> [options]
		config business account <user> <password> [options]
		add master instance <host> <port> [timeout_in_sec]
		show master instances
		add slave instance <host> <port> <master_server_id> [timeout_in_sec]
		grant config database
		add deploy server <master_server_id>
		add hash table <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>
		or
		add hash table from <sql_file_path> <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>
		update config time

+ 已有配置库(业务账号已混淆)

		load config database <database_name>
		decode config database
		load config database <database_name>
		show master instances
		add deploy server <master_server_id>
		add hash table <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>
		or
		add hash table from <sql_file_path> <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>
		confuse config database
		update config time

+ 已有配置库(业务账号未混淆)

		load config database <database_name>
		show master instances
		add deploy server <master_server_id>
		add hash table <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>
		or
		add hash table from <sql_file_path> <table_name> <table_count> [with hintId field <fiedl_name>] [to cluster <cluster_name>] in database <basic_database_name> <database_count>
		update config time

**注意：**

+ 建表的 SQL 语句最后一句必须以半角 ; 结束，且之后不能再有注释。
+ 该工具 cluster 仅支持单个单词，不能包含空白字符，但 DBProxy cluster 版的 cluster 支持多个单词，可以包含任意空白字符。
