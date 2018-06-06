#ifndef TableManagerBuilder_h_
#define TableManagerBuilder_h_

#include "TableManager.h"

class TableManagerBuilder
{
	struct ConstructureInfo
	{
		std::map<int, DatabaseInfoPtr> _dbCollection;
		std::map<int, DatabaseTaskQueuePtr> _dbTaskQueues;
		std::unordered_map<TableTaskHint, TableSplittingInfo*> _tableSplittingInfos;
		std::unordered_map<RangeTaskHint, std::vector<RangeSplittingInfo*>> _rangeSplittingInfos;
		
		~ConstructureInfo();

		DatabaseTaskQueuePtr findDatabaseTaskQueue(int masterServerId);
	};
	
	/*
		Init flow:
		
		1. add database info => _dbCollection
		2. add table info	=> _tableInfos
		3. add table splitting info => _tableSplittingInfos
		4. add range splitting info => _rangeSplittingInfos
		
		5. _dbCollection => _dbTaskQueues
		6. _tableSplittingInfos + _dbTaskQueues => _tableTaskQueues
		7. _rangeSplittingInfos + _dbTaskQueues => _rangedTaskQueues
		8. _dbInfos => enable Thread Pool
		9. clean _constructureInfo
	*/
	
	int64_t _splitSpan;
	TableManagerPtr _tableManager;
	
	std::unordered_map<TableHint, TableInfo*>	_tableInfos;
	std::unordered_map<TableTaskHint, DatabaseTaskQueuePtr> _tableTaskQueues;		//-- for hash
	std::unordered_map<RangeTaskHint, std::vector<RangeDatabaseNode*>> _rangedTaskQueues;
	std::set<DatabaseTaskQueuePtr> _usedTaskQueues;
	
	ConstructureInfo*	_constructureInfo;

	static int _perThreadPoolInitCount;
	static int _perThreadPoolAppendCount;
	static int _perThreadPoolPerfectCount;
	static int _perThreadPoolMaxCount;
	static int _perThreadPoolTempThreadLatencySeconds;

	bool init_status_check();
	bool init_step5_dbCollection_to_dbTaskQueues(TableManagerPtr oldTableManager);
	bool init_step6_tableSplittingInfos_dbTaskQueues_to_tableTaskQueues();
	bool init_step7_rangeSplittingInfos_dbTaskQueues_to_rangedTaskQueues();
	
public:
	static void config(int perThreadPoolInitCount, int perThreadPoolAppendCount, int perThreadPoolPerfectCount, int perThreadPoolMaxCount, int perThreadPoolTempThreadLatencySeconds);

	TableManagerBuilder(int64_t range_span, int secondary_split_table_number_base, int64_t update_time);
	~TableManagerBuilder();
	
	void addDatabaseInfo(DatabaseInfo *);
	bool addTableInfo(TableInfo *);
	void addTableSplittingInfo(TableSplittingInfo *);
	bool addRangeSplittingInfo(RangeSplittingInfo *);

	TableManagerPtr build(TableManagerPtr oldTableManager = nullptr);
};

#endif
