#include <iostream>
#include <string>
#include <string.h>
#include "hex.h"
#include "sha256.h"
#include "rijndael.h"
#include "AutoRelease.h"
#include "FormattedPrint.h"
#include "DBDeployer.h"

using namespace fpnn;

void genEncryptParams(const std::string& confUser, const std::string& confPassword, std::string& iv, std::string& key)
{
	std::string conf(confUser);
	conf.append(confPassword);

	unsigned char digest[32];
	sha256_checksum(digest, conf.data(), conf.length());

	iv.assign((char *)digest, 16);
	key.assign((char *)digest + 16, 16);
}

bool encrypt(const std::string& confUser, const std::string& confPassword, QueryResult& result)
{
	std::string ivStr, keyStr;
	genEncryptParams(confUser, confPassword, ivStr, keyStr);

	uint8_t iv[16];
	memcpy(iv, ivStr.data(), 16);

	size_t pos = 0;
	rijndael_context enCtx;
	rijndael_setup_encrypt(&enCtx, (const uint8_t *)keyStr.data(), 16);

	for (int i = 0; i < (int)result.rows.size(); i++)
	{
		std::string& orgUser = result.rows[i][0];
		std::string& orgPasswd = result.rows[i][1];

		if (orgUser.length() <= 0 || orgPasswd.length() <= 0)
		{
			std::cout<<"Error. Empty data user('"<<orgUser<<"') or passwd('"<<orgPasswd<<"') at row "<<i<<std::endl;
			return false;
		}

		char* ubuf = (char*)malloc(orgUser.length());
		char* pbuf = (char*)malloc(orgPasswd.length());
		
		AutoFreeGuard uafg(ubuf), pafg(pbuf);

		rijndael_cfb_encrypt(&enCtx, true, (uint8_t *)orgUser.data(), (uint8_t *)ubuf, orgUser.length(), iv, &pos);
		rijndael_cfb_encrypt(&enCtx, true, (uint8_t *)orgPasswd.data(), (uint8_t *)pbuf, orgPasswd.length(), iv, &pos);


		char* hexubuf = (char*)malloc(orgUser.length() * 2 + 1);
		char* hexpbuf = (char*)malloc(orgPasswd.length() * 2 + 1);

		AutoFreeGuard hexuafg(hexubuf), hexpafg(hexpbuf);

		int ulen = hexlify(hexubuf, ubuf, (int)orgUser.length());
		int plen = hexlify(hexpbuf, pbuf, (int)orgPasswd.length());

		result.rows[i][0].assign(hexubuf, ulen);
		result.rows[i][1].assign(hexpbuf, plen);
	}
	return true;
}

bool decrypt(const std::string& confUser, const std::string& confPassword, QueryResult& result)
{
	std::string ivStr, keyStr;
	genEncryptParams(confUser, confPassword, ivStr, keyStr);

	uint8_t iv[16];
	memcpy(iv, ivStr.data(), 16);

	size_t pos = 0;
	rijndael_context enCtx;
	rijndael_setup_encrypt(&enCtx, (const uint8_t *)keyStr.data(), 16);

	for (int i = 0; i < (int)result.rows.size(); i++)
	{
		std::string& orgUser = result.rows[i][0];
		std::string& orgPasswd = result.rows[i][1];

		if (orgUser.length() <= 0 || orgPasswd.length() <= 0)
		{
			std::cout<<"Error. Empty data user('"<<orgUser<<"') or passwd('"<<orgPasswd<<"') at row "<<i<<std::endl;
			return false;
		}

		char* ubuf = (char*)malloc(orgUser.length());
		char* pbuf = (char*)malloc(orgPasswd.length());

		int ulen = unhexlify(ubuf, orgUser.data(), (int)orgUser.length());
		int plen = unhexlify(pbuf, orgPasswd.data(), (int)orgPasswd.length());

		AutoFreeGuard uafg(ubuf), pafg(pbuf);

		if (ulen < 0 || plen < 0)
		{
			std::cout<<"Error. Error data user('"<<orgUser<<"') or passwd('"<<orgPasswd<<"') at row "<<i<<std::endl;
			return false;
		}

		char* deubuf = (char*)malloc(ulen);
		char* depbuf = (char*)malloc(plen);

		AutoFreeGuard deuafg(deubuf), depafg(depbuf);

		rijndael_cfb_encrypt(&enCtx, false, (uint8_t *)ubuf, (uint8_t *)deubuf, ulen, iv, &pos);
		rijndael_cfb_encrypt(&enCtx, false, (uint8_t *)pbuf, (uint8_t *)depbuf, plen, iv, &pos);

		result.rows[i][0].assign(deubuf, ulen);
		result.rows[i][1].assign(depbuf, plen);
	}

	return true;
}

bool updateServerInfoTable(MySQLClient &client, const std::string& dbname, QueryResult& result)
{
	for (int i = 0; i < (int)result.rows.size(); i++)
	{
		std::string updateSQL("update server_info set ");
		updateSQL.append("user='").append(result.rows[i][0]).append("', passwd='").append(result.rows[i][1]).append("' where server_id = ");
		updateSQL.append(result.rows[i][2]);

		QueryResult qr;
		if (client.query(dbname, updateSQL, qr) == false)
		{
			std::cout<<"exec sql ["<<updateSQL<<"] failed."<<std::endl;
			return false;
		}
	}

	return true;
}

void encryptConfigDB(MySQLClient &client, const std::string& dbname, const std::string& confUser, const std::string& confPassword)
{
	std::string sql("select user, passwd, server_id from server_info order by server_id asc");

	QueryResult result;
	if (client.query(dbname, sql, result) == false)
	{
		std::cout<<"exec sql ["<<sql<<"] failed."<<std::endl;
		return;
	}

	if (encrypt(confUser, confPassword, result) == false)
		return;

	if (updateServerInfoTable(client, dbname, result))
		std::cout<<"All Completed!"<<std::endl;
}

void decryptConfigDB(MySQLClient &client, const std::string& dbname, const std::string& confUser, const std::string& confPassword)
{
	std::string sql("select user, passwd, server_id from server_info order by server_id asc");

	QueryResult result;
	if (client.query(dbname, sql, result) == false)
	{
		std::cout<<"exec sql ["<<sql<<"] failed."<<std::endl;
		return;
	}

	if (decrypt(confUser, confPassword, result) == false)
		return;

	if (updateServerInfoTable(client, dbname, result))
		std::cout<<"All Completed!"<<std::endl;
}

void DBDeployer::confuseConfigDatabase()
{
	if (_configDB.dbName().empty())
	{
		std::cout<<"Please laod config database first."<<std::endl;
		return;
	}

	if (_confAccount.user.empty())
	{
		std::cout<<"Please config DBProxy account first."<<std::endl;
		return;
	}
	
	if (!mySQLIdleCheck(&_configDBClient))
		return;

	encryptConfigDB(_configDBClient, _configDB.dbName(), _confAccount.user, _confAccount.pwd);
}
void DBDeployer::decodeConfigDatabase()
{
	if (_configDB.dbName().empty())
	{
		std::cout<<"Please laod config database first."<<std::endl;
		return;
	}

	if (_confAccount.user.empty())
	{
		std::cout<<"Please config DBProxy account first."<<std::endl;
		return;
	}
	
	if (!mySQLIdleCheck(&_configDBClient))
		return;

	decryptConfigDB(_configDBClient, _configDB.dbName(), _confAccount.user, _confAccount.pwd);
}
