#ifndef Data_Router_Error_Info_h
#define Data_Router_Error_Info_h

#include "FPWriter.h"

namespace ErrorInfo
{
	using namespace fpnn;

	const int errorBase = 10000 * 10;

	const int disabledCode = errorBase + 403;
	const int notFoundCode = errorBase + 404;
	const int invalidParametersCode = errorBase + 422;
	const int internalErrorCode = errorBase + 500;
	const int MySQLExceptionCode = errorBase + 502;
	const int unconfiguredCode = errorBase + 503;
	const int serverBusyCode = errorBase + 513;

	const char* const raiser_MySQL = "mysql";
	const char* const raiser_DataRouter = "DBProxy";

	inline FPAnswerPtr unconfiguredAnswer(FPQuestPtr quest)
	{
		return FPAWriter::errorAnswer(quest, unconfiguredCode, "DB unconfigured.", raiser_DataRouter);
	}

	inline FPAnswerPtr tableNotFoundAnswer(FPQuestPtr quest)
	{
		return FPAWriter::errorAnswer(quest, notFoundCode, "Table not found.", raiser_DataRouter);
	}

	inline FPAnswerPtr invalidParametersAnswer(FPQuestPtr quest)
	{
		return FPAWriter::errorAnswer(quest, invalidParametersCode, "Invalid parameters.", raiser_DataRouter);
	}

	inline FPAnswerPtr negativeHintIdAnswer(FPQuestPtr quest)
	{
		return FPAWriter::errorAnswer(quest, invalidParametersCode, "HintId cannot be negative value.", raiser_DataRouter);
	}

	inline FPAnswerPtr disabledAnswer(FPQuestPtr quest)
	{
		return FPAWriter::errorAnswer(quest, disabledCode, "Disabled SQL statement type.", raiser_DataRouter);
	}

	inline FPAnswerPtr disabledAnswer(FPQuestPtr quest, const char* disableInfo)
	{
		return FPAWriter::errorAnswer(quest, disabledCode, disableInfo, raiser_DataRouter);
	}

	inline FPAnswerPtr serverBusyAnswer(FPQuestPtr quest)
	{
		return FPAWriter::errorAnswer(quest, serverBusyCode, "Corresponding query queue caught limitation.", raiser_DataRouter);
	}

	inline FPAnswerPtr MySQLExceptionAnswer(FPQuestPtr quest, int mysql_errno, const char* mysql_error, const char* mysql_sqlstate)
	{
		std::string ex("[MySQL Exception] errno: ");
		ex.append(std::to_string(mysql_errno)).append(", error: '");
		ex.append(mysql_error).append("', sql status: '");
		ex.append(mysql_sqlstate).append("'");
		return FPAWriter::errorAnswer(quest, MySQLExceptionCode, ex, raiser_MySQL);
	}
}

#endif
