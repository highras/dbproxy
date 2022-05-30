# DBProxy 接口说明

## 一、接口标识说明

接口描述格式说明：

| 标识 | 说明 |
|-----|------|
| => | 请求 |
| <= | 回复 |
| ~ | oneway 请求(无回复) |
| ? | 可选参数 |
| [] | 列表 |
| {} | 字典 |
| % | 格式标识 |

参数格式说明：

| 标识 | 说明 |
|-----|------|
| b | 布尔型 |
| B | Blob 类型 |
| d, i | 整形 |
| f | 浮点型 |
| s | 字符串 |
| x | b, B, d, i, f, s 某种类型 |
| X | b, B, d, i, f, s, 列表, 字典 某种类型 |


**详细描述请参见 [FPNN 协议说明](https://github.com/highras/fpnn/blob/master/doc/zh-cn/fpnn-protocol-introduction.md)**

**原始协议请参见 [Standard Version](../../standard/doc/DBProxy.protocol) [Cluster Version](../../cluster/doc/DBProxy.protocol)**

## 二、接口清单

| 接口名称 | 接口说明 |
|---------|---------|
| query | 查询数据（单 Shard） |
| iQuery | 查询数据（多 Shard 聚合查询） |
| sQuery | 查询数据（多 Shard 聚合查询） |
| splitInfo | 查询分库分表信息 |
| categoryInfo | 查询分段信息 |
| reformHintIds | 部分分库分表查询时，获取对应的等效hintId |
| allSplitHintIds | 全分库分表查询时，获取全部分表对应的hintId |
| refresh | 强制检查配置库是否更新。如果更新，加载新的配置。 |
| transaction | 事务操作 |
| sTransaction | 事务操作 |

## 三、接口明细

### query

* standard 版本

	=> query { hintId:%d, ?tableName:%s, sql:%s, ?params:[%s], ?master:%b }

* cluster 版本

	=> query { hintId:%d, ?tableName:%s, ?cluster:%s, sql:%s, ?params:[%s], ?master:%b }

* 参数说明

	+ **hintId**：分表/分库参考值。一般为分表的分表键，或分段的分段键。对于按区段类型的分表，负数将返回错误。
	+ **tableName**：提供 tableName，可以加快 DBProxy 查询速度。如果不提供，DBProxy 将会从 sql 参数中获取，而这将影响查询速度和效率。
	+ **cluster**：数据库业务分组标志。如果缺失，默认为空。
	+ **params**：参数化SQL查询时的参数值。如果sql中不包含'?'占位符，params不需传递。
	+ **master**：当后端MySQL为主从配置时，是否强制读任务为主库任务(强制查询读主库)。如果缺失，默认 false。

* 返回

	+ 当查询为 select, desc, describe, explain 时，返回

			<= { fields:[%s], rows:[[%s]] }

	+ 当查询为 insert, update, replace, delete 时，返回

			<= { affectedRows:%i, insertId:%i }

* 参数化SQL查询

	如果sql参数中包含占位符'?'，则视为参数化SQL查询。此时，params中的字符串将依次替换sql参数中的'?'。

	例：  
	sql: "select ? from ? where uid=?", params: ["name", "tbl_users", "123456"]，替换后为："select name from tbl_users where uid=123456"

	如果sql参数中'?'前后均为单引号" ' "，则对应的params字符串，在替换时，将进行 escape 处理。如果不被单引号" ' "所包围，则不进行 escape 处理。

	例：  
	sql: "select ? from ? where uid=? and name = '?' ",params:["age", "tbl_users", "123456", "abc"]，则替换后为："select age from tbl_users where uid=123456 and name='abc' "

	其中的"age", "tbl_users", "123456"将不会被进行 escape 处理，而 "abc" 则会被 escape 处理。

* 注意

	1. SQL 中，仅允许 select、update、insert、replace、delete、desc、describe、explain 8种操作。（DBProxy Manager 版本无此限制。）

	1. 参数化SQL查询时，字符串请使用" ' "包围。如果" ' "不出现在SQL模板中，则对应的字符串不会进行 escape 处理。

		例：

		sql: "select age from tbl_users where name = '?' ", params:["abc"]，则替换后为："select age from tbl_users where name='abc' ", "abc" 会被 escape 处理。

		sql: "select age from tbl_users where name =? ", params:["'abc'"]，则替换后为："select age from tbl_users where name='abc' ", "'abc'" 不会被 escape 处理。

		sql: "select age from tbl_users where name =? ", params:["abc"]，则替换后为："select age from tbl_users where name=abc ", SQL 执行出错。

	1. 与 Prepared Statements/PDO仿真预处理 等的区别

		从目的而言，Prepared Statements/PDO仿真预处理 是为了提升SQL执行效率，以及提高安全性，防止SQL注入；参数化查询的目的是为使用者提供便利，增加使用的灵活性，尽量的兼容 Prepared Statements/PDO仿真预处理 的使用方式，提供有限的安全处理。

		从实现层面和数据库端操作层面而言，二者完全不同。

		从用户使用层面而言，除了以下几点细节外，其余可视为等同：

		+ 基于功能目的不同，Prepared Statements/PDO仿真预处理 不能参数化 表名 和 操作类型(select、insert、delete、update、replace 等)，参数化查询可以。
		+ Prepared Statements/PDO仿真预处理 会自动识别参数类型，为字符串加上" ' "，参数化查询不会。
		+ Prepared Statements/PDO仿真预处理 会对所有字符串进行 escape 处理，参数化查询只对" '?' "占位的字符串进行 escape 处理，对" ? "占位的字符串不做 escape 处理。



### iQuery & sQuery

* standard 版本

	=> iQuery { hintIds:[%d], ?tableName:%s, sql:%s, ?params:[%s], ?master:%b }

	或者

	=> sQuery { hintIds:[%s], ?tableName:%s, sql:%s, ?params:[%s], ?master:%b }

* cluster 版本

	=> iQuery { hintIds:[%d], ?tableName:%s, ?cluster:%s, sql:%s, ?params:[%s], ?master:%b }

	或者

	=> sQuery { hintIds:[%s], ?tableName:%s, ?cluster:%s, sql:%s, ?params:[%s], ?master:%b }


* 参数说明

	+ 基本类似于 query 接口。
	+ 如果 hintIds 只有一个元素，则自动转换为对应的 query 操作。
	+ 如果 hintIds 为多个元素，在 DBProxy 内部重整后，可能会变成多个 shard 的聚合查询。
	+ 如果 hintIds 为空，则会转变成所有 shard 的聚合查询。

* 注意

	+ hintIds 不能缺省。虽然不传递 hintIds 和 hintIds 为空有着相同的含义，但如果允许 hintIds 缺省的话，如果用户不小心将 hintIds 写成 hintId，则原本期望的在部分 shard 上执行的聚合查询将会变成所有 shard 上的聚合查询。
	+ sQuery 当 hintIds 不为空时，仅允许作用于 hash 分表类型的数据表。不允许作用于区段分库分表的数据表。
	+ 当hintIds 成员数量不为1时，iQuery 和 sQuery 只允许 select 操作。（DBProxy Manager 版本无此限制。）
	+ 多个 shard 聚合查询时，**group by** 和 **order by** 将会**失效**。因为在多个 shard 返回的数据集间，DBProxy 不会再做重整处理，而只是简单地将多个数据集整合。
	+ 如果想进行 hintId 为字符串类型的查询，则必须使用该接口。query hintId 仅支持整数类型。

* 返回

	+ 当查询为 select, desc, describe, explain 时，返回

		+ iQuery 返回

				<=  { fields:[%s], rows:[[%s]], ?failedIds:[%d], ?invalidIds:[%d] }

		+ sQuery 返回

				<=  { fields:[%s], rows:[[%s]], ?failedIds:[%s] }

		+ 如果 iQuery 或 sQuery hintIds 为空，返回

				<=  { fields:[%s], rows:[[%s]], ?failedIds:[%d] }

	+ 当查询为 insert, update, replace, delete 时，返回

		+ iQuery 返回
		
				<=  { results:[[%d, %d, %d]], ?failedIds:[%d], ?invalidIds:[%d] }

		+ sQuery 返回

				<=  { results:[[%d, %d, %d]], ?failedIds:[%s] }

		+ 如果 iQuery 或 sQuery hintIds 为空，返回

				<=  { results:[[%d, %d, %d]], ?failedIds:[%d] }

* 注意

	+ 如果每个 shard 上的查询都执行成功，则没有 failedIds 一项。
	+ 如果 hintIds 为空，返回结果中，failedIds 内的整型为等效的 shard ID。可通过该 id 作为 hintId，访问对应的 shard，重放操作。
	+ 对于 insert, update, replace, delete 等类型的查询，返回结果 result 中的整型三元组，依次为：等效的 shard id，对应 shard 上的 affectedRows，对应shard 上的 insertId。
	+ 当hintIds成员数量不为1时，desc，describe，explain，insert, update, replace, delete 等类型的查询默认禁止。（DBProxy Manager 版本无此限制。）

* 参数化SQL查询

	与 query 接口处理相同。请参见 query 接口说明。


### splitInfo

* standard 版本

	=> splitInfo { tableName:%s }

* cluster 版本

	=> splitInfo { tableName:%s, ?cluster:%s }

* 返回值

	+ 当分表类型为按区段分库分表时，返回

			<= { splitByRange:true, span:%d, count:%d, databaseCategory:%s, splitHint:%s, secondarySplit:%b, secondarySplitSpan:%d }

		count 为该表分段的数目。不计奇偶分库和库内二次分段分表。

	+ 当分表类型为 Hash 分表时，返回

			<= { splitByRange:false, tableCount:%d, splitHint:%s }


### categoryInfo

仅对区段分库分表使用。

* standard 版本

		=> categoryInfo { databaseCategory:%s }
		<= { splitCount:%d, oddEvenCount:%d, ?oddEvenIndexes:[%d] }

* cluster 版本

		=> categoryInfo { databaseCategory:%s, ?cluster:%s }
		<= { splitCount:%d, oddEvenCount:%d, ?oddEvenIndexes:[%d] }

当 oddEvenCount 为 0 时，返回结果里，oddEvenIndexes 不存在。


### reformHintIds

* standard 版本

		=> reformHintIds { tableName:%s, hintIds:[%d] }
		<= { hintPairs:[ [%d,[%d]] ], invalidIds:[%d] }

* cluster 版本

		=> reformHintIds { tableName:%s, ?cluster:%s, hintIds:[%d] }
		<= { hintPairs:[ [%d,[%d]] ], invalidIds:[%d] }

+ **hintPairs**

	每个 [%d, [%d]] 单元里，第一个 %d 是对应分库分表等效的hintId，后面的 [%d] 为输入的 hintIds 中，在该等效 hintId 对应的分库分表中有效的 hintIds。


### allSplitHintIds

* standard 版本

		=> allSplitHintIds { tableName:%s }
		<= { hintIds:[%d] }

* cluster 版本

		=> allSplitHintIds { tableName:%s, ?cluster:%s }
		<= { hintIds:[%d] }

### refresh

	=> refresh {}
	<= {}

当配置库修改后，如果需要立刻加载新的配置信息到DBProxy，则发送该指令。

默认情况下，DBProxy 检查和加载新配置库内容取决于 DBProxy 的配置文件。


### transaction & sTransaction

* standard 版本

		=> transaction { hintIds:[%d], tableNames:[%s], sqls:[%s] }
		<= {}

		=> sTransaction { hintIds:[%s], tableNames:[%s], sqls:[%s] }
		<= {}

* cluster 版本

		=> transaction { hintIds:[%d], tableNames:[%s], ?cluster:%s, sqls:[%s] }
		<= {}

		=> sTransaction { hintIds:[%s], tableNames:[%s], ?cluster:%s, sqls:[%s] }
		<= {}


**注意：**

+ 仅支持同一 shard 上的事务操作。
+ 对于 range 类型分表，hintId 要求为正数或者0值，负数将返回错误。
+ 事务成功，会自动提交；事务失败，会自动回滚。
+ 事务中，仅允许 select、update、insert、replace、delete、desc、describe、explain 8种操作。
+ START TRANSACTION, BEGIN, COMMIT, ROLLBACK 等被禁止，由 DBProxy 自动添加。
+ sTransaction 仅支持 hash 分表，不支持区段分库分表。


## 四、错误代码

以上请求，如果发生错误，则会返回字典：`{ code:%d, ex:%s }`

错误代码为 FPNN 框架错误代码加上以下错误代码：

+ 100403: Invalid SQL statement, or disable operations. 
+ 100404: Table is not found.
+ 100422: Invalid Parameters.
+ 100500: Internal error.
+ 100502: MySQL error.
+ 100503: Unconfigured.
+ 100513: Corresponding query queue caught limitation.

FPNN 错误代码请参见：[FPNN 错误代码](https://github.com/highras/fpnn/blob/master/doc/zh-cn/fpnn-error-code.md)