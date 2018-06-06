# DBProxy 账号与安全

## 一、账号

DBProxy 正常需要三组账号：

1. 管理员账号，拥有所有数据库表的所有权限

	该账号用于数据库管理、维护，以及部署工具创建和修改数据库表结构。

	**DBProxy 及相关管理工具不会记录或保存该账号。**

1. DBProxy 配置库账号

	该账号仅能访问 DBProxy 配置库，且只有 select 权限。

	该账号会被以**明文**的形式，写入 DBProxy 的配置文件中。

1. DBProxy 业务数据库账号

	该账号仅能访问对应的业务数据库，且只拥有 select，update，insert，delete 四种权限。（DBProxy Manager 版本无此限制。）

	该账号将被记录入 DBProxy 配置库的 server_info 表中。

	如果启动了混淆功能，则以密文显示，否则以明文显示。


## 二、账号混淆

启动混淆后，DBProxy 配置库 server_info 表中的业务数据账号信息，将以密文显示。

* 注意：

	* 用 DBConfigConfuser 工具启动混淆前，先备份 server_info 表，或者备份密码信息；
	* 混淆后，server_info 表 server_id, user, passwd 三项，不允许有任何改动、删除、或者新增，否则混淆将无法还原，DBProxy 将无法读取；
	* 如果改动 DBProxy 的配置库账号，需要重新混淆，否则 DBProxy 无法还原混淆的账户信息。


* 混淆方案：

	假设配置库的账户为 account，密码为 password

	bit256 = sha256(account + password)

	将 bit256 拆分为两个 bit128，作为 vi 和 key，加密 server_info 中的 user & passwd 字段

	多个行按照 server_id 大小，做流式加密


## 三、业务安全

DBProxy 每个版本都有普通版和 DBA 专用的管理员版本。

出于业务安全的角度，业务应该使用普通版本。  
普通版本只能执行 select、update、insert、replace、delete、desc、describe、explain 8种操作。  
且普通版本的 iQuery 和 sQuery 接口，当hintIds 成员数量不为1时，只允许 select 操作。

DBA 使用 DBProxy Manager 版本。  
随用随起，不需长期启动。  
DBProxy Manager 版本支持 alter table 语句、show create table 语句。  
DBProxy Manager 版本的 iQuery 和 sQuery 接口，当hintIds 成员数量不为1时，可以执行 select、update、insert、replace、delete、desc、describe、explain、alter table、show create table 语句。

DBProxy Manager 版本可执行文件名称为：DBProxyMgr。
