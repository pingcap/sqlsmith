#include "mysql.hh"
#include <cstring>
#include <regex>
#include <iostream>
#include <fstream>

using namespace std;

void mysql_connection::parse_connection_string(std::string &conninfo) {
	// URI is mysql://root[:password]@host:port/database
	size_t pos = conninfo.find("mysql://");
	if (pos != 0) {
		throw std::runtime_error("MySQL URI must be 'mysql://root[:password]@host:port/database'");
	}

	size_t at_pos = conninfo.find("@");
	user = conninfo.substr(8, at_pos - 8);
	pos = user.find(":");
	if (pos != std::string::npos) {
		password = user.substr(pos + 1);
		user = user.substr(0, pos);
	}

	size_t db_pos = conninfo.find("/", at_pos);
	if (db_pos != std::string::npos) {
		db = conninfo.substr(db_pos + 1);
	} else {
		db_pos = conninfo.length();
	}
	host = conninfo.substr(at_pos + 1, db_pos - at_pos - 1);
	pos = host.find(":");
	if (pos != std::string::npos) {
		port = atoi(host.substr(pos + 1).c_str());
		host = host.substr(0, pos);
	}
}

mysql_connection::mysql_connection(std::string &conninfo) {
  if (conninfo == "") {
    return;
  }
  mysql_init(&mysql);
  parse_connection_string(conninfo);

  if (mysql_real_connect(&mysql, host.c_str(), user.c_str(), password.c_str(), db.c_str(), port, NULL, 0) == NULL) {
    throw std::runtime_error(mysql_error(&mysql));
  }
}

mysql_connection::~mysql_connection() {
  mysql_close(&mysql);
}

// Refer https://github.com/louishust/sqlsmith/blob/mysql/mysql.cc#L73
// May improve later
std::string parse_column_type(const char* column_type) {
  if (!strcmp(column_type, "TINYINT") ||
      !strcmp(column_type, "SMALLINT") ||
      !strcmp(column_type, "MEDIUMINT") ||
      !strcmp(column_type, "INT") ||
      !strcmp(column_type, "BIGINT")) {
    return std::string("INTEGER");
  }

  if (!strcmp(column_type,"DOUBLE") ||
      !strcmp(column_type,"FLOAT") ||
      !strcmp(column_type,"NUMERIC") ||
      !strcmp(column_type,"DECIMAL")) {
    return std::string("DOUBLE");
  }
  if (!strcmp(column_type,"VARCHAR") ||
      !strcmp(column_type,"CHAR") ||
      !strcmp(column_type,"TEXT") ||
      !strcmp(column_type,"TINYTEXT") ||
      !strcmp(column_type,"MEDIUMTEXT") ||
      !strcmp(column_type,"LONGTEXT")) {
    return std::string("VARCHAR");
  }

  if (!strcmp(column_type,"DATE") ||
      !strcmp(column_type,"TIME") ||
      !strcmp(column_type,"DATETIME") ||
      !strcmp(column_type,"TIMESTAMP") ||
      !strcmp(column_type,"YEAR")) {
    return std::string("TIMESTAMP");
  }

  if (!strcmp(column_type,"BIT")) {
    return std::string("BIT");
  }

  if (!strcmp(column_type,"BINARY") ||
      !strcmp(column_type,"BLOB") ||
      !strcmp(column_type,"TINYBLOB") ||
      !strcmp(column_type,"MEDIUMBLOB") ||
      !strcmp(column_type,"LONGBLOB") ||
      !strcmp(column_type,"VARBINARY")) {
    return std::string("BINARY");
  }

  if (!strcmp(column_type,"ENUM")) {
    return std::string("ENUM");
  }

  if (!strcmp(column_type,"SET")) {
    return std::string("SET");
  }

  char errmsg[64];
  sprintf(errmsg, "Unhandled data type: %s", column_type);
  throw std::runtime_error(errmsg);
}

schema_mysql::schema_mysql(std::string &conninfo, bool no_catalog)
  : mysql_connection(conninfo)
{
  if (conninfo == "") {
    return;
  }
  (void)no_catalog;
  MYSQL_RES *result;
  MYSQL_ROW row;
  char query[2048];
  sprintf(query, "SELECT  TABLE_NAME, TABLE_SCHEMA, TABLE_TYPE FROM information_schema.tables WHERE TABLE_SCHEMA ='%s'", db.c_str());

  cerr << "Loading tables...";
  int rc = mysql_real_query(&mysql, query, strlen(query));
  if (rc) {
    throw std::runtime_error(mysql_error(&mysql));
  }
  result = mysql_store_result(&mysql);

  while ((row = mysql_fetch_row(result))) {
    string table_name;
    string schema_name;
    string column_type;
    bool insertable;
    bool base_table;
    if (!strcmp(row[2], "BASE TABLE")) {
        insertable = true;
        base_table = true;
    } else if (!strcmp(row[2], "VIEW")) {
        insertable = false;
        base_table = false;
    } else {
      continue;
    }

    cout << row[0] << row[1] << insertable << base_table << endl;

    table tab(row[0], row[1], insertable, base_table);
    tables.push_back(tab);
  }
  mysql_free_result(result);
  cerr << "done." << endl;

  cerr << "Loading columns and constraints...";
  for (auto t = tables.begin(); t != tables.end(); ++t) {
    sprintf(query, "SELECT COLUMN_NAME, upper(DATA_TYPE) FROM information_schema.columns WHERE TABLE_SCHEMA='%s' AND TABLE_NAME='%s';", t->schema.c_str(), t->name.c_str());
    rc = mysql_real_query(&mysql, query, strlen(query));
    if (rc) {
      throw std::runtime_error(mysql_error(&mysql));
    }
    result = mysql_store_result(&mysql);
    while ((row = mysql_fetch_row(result))) {
      string column_type = parse_column_type(row[1]);
      column c(row[0], sqltype::get(column_type));
      t->columns().push_back(c);
    }
    mysql_free_result(result);
  }
  cerr << "done." << endl;


#define BINOP(n,t) do {op o(#n,sqltype::get(#t),sqltype::get(#t),sqltype::get(#t)); register_operator(o); } while(0)

  BINOP(*, INTEGER);
  BINOP(/, INTEGER);

  BINOP(+, INTEGER);
  BINOP(-, INTEGER);

  BINOP(>>, INTEGER);
  BINOP(<<, INTEGER);

  BINOP(&, INTEGER);
  BINOP(|, INTEGER);

  BINOP(<, INTEGER);
  BINOP(<=, INTEGER);
  BINOP(>, INTEGER);
  BINOP(>=, INTEGER);

  BINOP(=, INTEGER);
  BINOP(<>, INTEGER);
  BINOP(IS, INTEGER);
  BINOP(IS NOT, INTEGER);

  BINOP(AND, INTEGER);
  BINOP(OR, INTEGER);

#define FUNC(n,r) do {							\
    routine proc("", "", sqltype::get(#r), #n);				\
    register_routine(proc);						\
  } while(0)

#define FUNC1(n,r,a) do {						\
    routine proc("", "", sqltype::get(#r), #n);				\
    proc.argtypes.push_back(sqltype::get(#a));				\
    register_routine(proc);						\
  } while(0)

#define FUNC2(n,r,a,b) do {						\
    routine proc("", "", sqltype::get(#r), #n);				\
    proc.argtypes.push_back(sqltype::get(#a));				\
    proc.argtypes.push_back(sqltype::get(#b));				\
    register_routine(proc);						\
  } while(0)

#define FUNC3(n,r,a,b,c) do {						\
    routine proc("", "", sqltype::get(#r), #n);				\
    proc.argtypes.push_back(sqltype::get(#a));				\
    proc.argtypes.push_back(sqltype::get(#b));				\
    proc.argtypes.push_back(sqltype::get(#c));				\
    register_routine(proc);						\
  } while(0)

  FUNC(last_insert_rowid, INTEGER);

  FUNC1(abs, INTEGER, INTEGER);
  FUNC1(hex, VARCHAR, VARCHAR);
  FUNC1(length, INTEGER, VARCHAR);
  FUNC1(lower, VARCHAR, VARCHAR);
  FUNC1(ltrim, VARCHAR, VARCHAR);
  FUNC1(rtrim, VARCHAR, VARCHAR);
  FUNC1(trim, VARCHAR, VARCHAR);
  FUNC1(quote, VARCHAR, VARCHAR);
  FUNC1(round, INTEGER, DOUBLE);
  FUNC1(rtrim, VARCHAR, VARCHAR);
  FUNC1(trim, VARCHAR, VARCHAR);
  FUNC1(upper, VARCHAR, VARCHAR);

  FUNC2(instr, INTEGER, VARCHAR, VARCHAR);
  FUNC2(substr, VARCHAR, VARCHAR, INTEGER);

  FUNC3(substr, VARCHAR, VARCHAR, INTEGER, INTEGER);
  FUNC3(replace, VARCHAR, VARCHAR, VARCHAR, VARCHAR);


#define AGG(n,r, a) do {						\
    routine proc("", "", sqltype::get(#r), #n);				\
    proc.argtypes.push_back(sqltype::get(#a));				\
    register_aggregate(proc);						\
  } while(0)

  AGG(avg, INTEGER, INTEGER);
  AGG(avg, DOUBLE, DOUBLE);
  AGG(count, INTEGER, INTEGER);
  AGG(group_concat, VARCHAR, VARCHAR);
  AGG(max, DOUBLE, DOUBLE);
  AGG(max, INTEGER, INTEGER);
  AGG(sum, DOUBLE, DOUBLE);
  AGG(sum, INTEGER, INTEGER);

  booltype = sqltype::get("INTEGER");
  inttype = sqltype::get("INTEGER");

  internaltype = sqltype::get("internal");
  arraytype = sqltype::get("ARRAY");

  true_literal = "1";
  false_literal = "0";

  generate_indexes();
  mysql_close(&mysql);
}

dut_mysql::dut_mysql(std::string& conninfo)
    : mysql_connection(conninfo)
{}

void dut_mysql::test(const std::string &stmt) {
  // remove tablesample syntax which mysql don't support
  std::string r_stmt = std::regex_replace(stmt, std::regex("tablesample.+?\n"), " \n");
  r_stmt = std::regex_replace(r_stmt, std::regex("\n"), " ");
  r_stmt = std::regex_replace(r_stmt, std::regex("\\s+"), " ");
  // cout << r_stmt << endl;
	MYSQL_RES *result;
  int rc = mysql_real_query(&mysql, r_stmt.c_str(), r_stmt.length());
  if(rc){
  	unsigned int err_num = mysql_errno(&mysql);
  	const char* err = mysql_error(&mysql);
  	// 1064: Syntax error
  	// TODO: may support handling following errors
  	// 1054: Unknown column 'subq_0.c0' in 'field list'
  	// 1247: Reference 'c4' not supported (forward reference in item list)
  	// 1052: Column 'subq_1.c0' in field list is ambiguous
  	// 1093: You can't specify target table 't' for update in FROM clause
  	if (err_num == 1064 || err_num == 1054 ||
  		err_num == 1247 || err_num == 1052 ||
  		err_num == 1093) {
      throw dut::syntax(err);  		
  	} 
  	else {
	    throw std::runtime_error(err);
  	}
  } else {
    ofstream sqlfile;
    sqlfile.open("./sql/success-sql.txt", std::ios_base::app);
    sqlfile << r_stmt << endl;
    sqlfile.close();
  }
  result = mysql_store_result(&mysql);
  mysql_free_result(result);
}