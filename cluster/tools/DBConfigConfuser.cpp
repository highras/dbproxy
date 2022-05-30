//#include <algorithm>
#include <iostream>
#include <string>
#include <string.h>
#include "hex.h"
#include "sha256.h"
#include "rijndael.h"
#include "AutoRelease.h"
#include "FormattedPrint.h"
#include "../DBProxy/MySQLClient.cpp"

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

int findColumnIndex(const char* column, QueryResult& result)
{
	for (int i = 0; i < (int)result.fields.size(); i++)
		if (result.fields[i] == column)
			return i;

	return -1;
}

bool decrypt(const std::string& confUser, const std::string& confPassword, QueryResult& result)
{
	int userIdx = findColumnIndex("user", result);
	int passwdIdx = findColumnIndex("passwd", result);

	if (userIdx < 0 || passwdIdx < 0)
	{
		std::cout<<"Error. Miss column user("<<userIdx<<") or passwdIdx("<<passwdIdx<<")"<<std::endl;
		return false;
	}

	std::string ivStr, keyStr;
	genEncryptParams(confUser, confPassword, ivStr, keyStr);

	uint8_t iv[16];
	memcpy(iv, ivStr.data(), 16);

	size_t pos = 0;
	rijndael_context enCtx;
	rijndael_setup_encrypt(&enCtx, (const uint8_t *)keyStr.data(), 16);

	for (int i = 0; i < (int)result.rows.size(); i++)
	{
		std::string& orgUser = result.rows[i][userIdx];
		std::string& orgPasswd = result.rows[i][passwdIdx];

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

		result.rows[i][userIdx].assign(deubuf, ulen);
		result.rows[i][passwdIdx].assign(depbuf, plen);
	}

	return true;
}

void encryptConfigDB(MySQLClient &client, const std::string& dbname, const std::string& region, const std::string& confUser, const std::string& confPassword)
{
	std::string sql("select user, passwd, server_id from server_info");
	if (region.length())
		sql.append("_").append(region);
	sql.append(" order by server_id asc");

	QueryResult result;
	if (client.query(dbname, sql, result) == false)
	{
		std::cout<<"exec sql ["<<sql<<"] failed."<<std::endl;
		return;
	}

	if (encrypt(confUser, confPassword, result) == false)
		return;

	std::string sqlTemplate("update server_info");
	if (region.length())
		sqlTemplate.append("_").append(region);
	sqlTemplate.append(" set ");

	for (int i = 0; i < (int)result.rows.size(); i++)
	{
		std::string updateSQL(sqlTemplate);
		updateSQL.append("user='").append(result.rows[i][0]).append("', passwd='").append(result.rows[i][1]).append("' where server_id = ");
		updateSQL.append(result.rows[i][2]);

		QueryResult qr;
		if (client.query(dbname, updateSQL, qr) == false)
		{
			std::cout<<"exec sql ["<<updateSQL<<"] failed."<<std::endl;
			return;
		}
	}

	std::cout<<"All Completed!"<<std::endl;
}

void decryptConfigDB(MySQLClient &client, const std::string& dbname, const std::string& region, const std::string& confUser, const std::string& confPassword)
{
	std::string sql("select * from server_info");
	if (region.length())
		sql.append("_").append(region);
	sql.append(" order by server_id asc");

	QueryResult result;
	if (client.query(dbname, sql, result) == false)
	{
		std::cout<<"exec sql ["<<sql<<"] failed."<<std::endl;
		return;
	}

	if (decrypt(confUser, confPassword, result))
		printTable(result.fields, result.rows);
}

int main(int argc, const char* argv[])
{
	MySQLClient::MySQLClientInit();

	if (argc == 10 && strcmp(argv[6], "encrypt") == 0)
	{
		std::string dbname(argv[5]);
		MySQLClient client(argv[1], atoi(argv[2]), argv[3], argv[4], dbname);

		encryptConfigDB(client, dbname, argv[7], argv[8], argv[9]);
	}
	else if (argc == 8 && strcmp(argv[6], "decrypt") == 0)
	{
		std::string dbname(argv[5]);
		MySQLClient client(argv[1], atoi(argv[2]), argv[3], argv[4], dbname);

		decryptConfigDB(client, dbname, argv[7], argv[3], argv[4]);
	}
	else
	{
		std::cout<<"Usage: "<<argv[0]<<" host port root-user root-password config-dbname encrypt region conf-user conf-password"<<std::endl;
		std::cout<<"Usage: "<<argv[0]<<" host port conf-user conf-password config-dbname decrypt region"<<std::endl;
	}

	MySQLClient::MySQLClientEnd();
	return 0;
}
