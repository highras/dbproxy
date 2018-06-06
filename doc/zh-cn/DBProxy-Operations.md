# DBProxy 运维管理

## 一、账号相关

请参考 [DBProxy 账号与安全](doc/zh-cn/DBProxy-Account-Security.md)


## 二、部署数据库

请使用 DBDeployer 操作。

具体请参见[DBProxy 管理工具](DBProxy-Tools.md)中，DBDeployer 相关内容。

**注意**

+ hash 分库分表与区段分库

	因为区段分库在运行一段时间后，会导致数据库压力和热点不均匀，因此强烈建议使用 hash 分库分表。  
	因此区段分库的操作，此处不再介绍。

+ 奇偶分库与二级分表

	目前奇偶分库与二级分表**仅区段分库**支持，因此此处不做过多介绍。

+ 核查数据表

	对于新创建的数据表，请使用 DBTableChecker 或 DBTableStrictChecker 进行检查。

+ 新增数据表生效

	1. 请使用 DBDeployer 的 update config time 命令，更新配置库更新时间。

	1. 然后：

		+ 等待配置项 DBProxy.ConfigureDB.checkInterval 指定的时间过后，DBProxy 自动加载新的数据表。
		+ 或使用 DBRefresher 强制每个 DBProxy 立刻加载新的数据表。


## 三、修改数据表结构

1. 请使用普通版 DBProxy 的配置文件，启动 DBProxyMgr 服务：

	+ 拷贝 DBProxy 的配置文件，如有必要，请改动端口号；

	+ 修改配置文件中 FPNN.server.idle.timeout 项，确保 alter table 操作时，链接 idle 不会超时；

		注意：FPNN.server.idle.timeout 默认是 60 秒，但如果对数据量巨大的表进行 alter 操作，可能需要超过 15 分钟的超时时间。

	+ 确保配置文件指向的配置库内配置的账号，具有 alter table 的权限。

		* 如果有必要，拷贝生成新的临时配置库，并将该库内的账号，赋予 alter table 的权限。

		* 并将配置文件中的配置库地址和账号信息，指向临时配置库。

	+ 修改其他必要的配置，比如日志等级，日志输出等；

1. 修改数据表

	请使用 DBParamsQuery 发送 alter table 指令。

	可以进行 multi-shardings 或者 all shardings 并发修改。

	all shardings 请用 -i "" 作为 hintIds 参数即可。

	**注意**：请使用 -timeout 参数指定查询超时。

1. 核查数据表

	如果是字段相关的改动，请使用 DBTableStrictChecker 进行检查；

	如果是字符集设置的改动，请使用 DBParamsQuery 发送 show create sql 命令，然后 grep 关键字，并 wc -l 统计，等方式，进行检查。

1. 修改数据表生效

		1. 请使用 DBDeployer 的 update config time 命令，更新配置库更新时间。

		1. 然后：

			+ 等待配置项 DBProxy.ConfigureDB.checkInterval 指定的时间过后，DBProxy 自动加载新的数据表。
			+ 或使用 DBRefresher 强制每个 DBProxy 立刻加载新的数据表。
