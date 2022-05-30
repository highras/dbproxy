# DBProxy 配置说明

## 一、配置库

配置库请参见：

+ [Standard Version](../../standard/DBProxy/configurationSQL/configurationTables.sql)

+ [Cluster Version](../../cluster/DBProxy/configurationSQL/configurationTables.sql)

具体部署，可使用**DBDeployer**进行部署。请参考[DBProxy 管理工具](DBProxy-Tools.md)中，DBDeployer 相关内容。

## 二、配置文件

配置文件模版请参见：
* [Standard Version](../../standard/DBProxy/DBProxy.conf)
* [Cluster Version](../../cluster/DBProxy/DBProxy.conf)

### 配置条目说明

1. FPNN 框架通用配置

	FPNN 框架通用配置，包含，但不限于以下配置项，具体请参考：[FPNN 标准配置模板](https://github.com/highras/fpnn/blob/master/doc/conf.template)

	+ **FPNN.server.listening.ip**
	+ **FPNN.server.listening.port**
	+ **FPNN.server.name**
	+ **FPNN.server.log.level**
	+ **FPNN.server.log.endpoint**
	+ **FPNN.server.log.route**

	**如需监听 IPv6 地址和端口**，亦请参考 [FPNN 标准配置模板](https://github.com/highras/fpnn/blob/master/doc/conf.template)

1. DBProxy 配置库配置

	+ **DBProxy.ConfigureDB.host**

		配置库地址

	+ **DBProxy.ConfigureDB.port**

		配置库端口

	+ **DBProxy.ConfigureDB.timeout**

		配置库操作超时时间。单位：秒

	+ **DBProxy.ConfigureDB.databaseName**

		配置库库名

	+ **DBProxy.ConfigureDB.username**

		配置库账号

	+ **DBProxy.ConfigureDB.password**

		配置库密码

	+ **DBProxy.ConfigureDB.checkInterval**

		配置库自动更新检查间隔。单位：秒

	+ **DBProxy.ConfigureDB.enableConfuse**

		是否启用业务库账号混淆


1. DBProxy 链接池配置

	**DBProxy 对每个 MySQL 实例保持一个链接池。**

	+ **DBProxy.perThreadPool.InitThreadCount**

		链接池初始链接数量（未使用时不会实际链接）。

	+ **DBProxy.perThreadPool.AppendThreadCount**

		链接池单次增量。

	+ **DBProxy.perThreadPool.PerfectThreadCount**

		最大**常驻**链接池的链接数量。

	+ **DBProxy.perThreadPool.MaxThreadCount**

		链接池最大链接数量。

	+ **DBProxy.perThreadPool.temporaryThread.latencySeconds**

		超出链接池最大**常驻**链接数量时，多余链接在空闲该指定时间后，自动关闭。单位：秒

	+ **DBProxy.perThreadPool.readQueue.MaxLength**

		链接池读队列(从库队列)最大待处理任务数量。

	+ **DBProxy.perThreadPool.writeQueue.MaxLength**

		链接池写队列(主库队列)最大待处理任务数量。

1. MySQL 链接配置

	+ **DBProxy.mySQLPingInterval**

		链接存活检查间隔。单位：秒

	+ **DBProxy.connection.characterSet.name**

		链接使用的字符集：utf8、utf8mb4、binary

1. FPZK集群配置(**可选配置**)

	**未配置以下诸项时，DBProxy 将不会向 FPZK 注册。**


	+ **FPZK.client.fpzkserver_list**

		FPZK 服务集群地址列表。**半角**逗号分隔。

	+ **FPZK.client.project_name**

		DBProxy 从属的项目名称

	+ **FPZK.client.project_token**

		DBProxy 从属的项目 token

	+ **FPNN.server.cluster.name**

		DBProxy 在 FPZK 中的业务分组名称。**可选配置**。  
		**注：DBProxy 在 FPZK 中的业务分组，和 DBProxy 代理的数据库的业务分组没有任何关系。**

