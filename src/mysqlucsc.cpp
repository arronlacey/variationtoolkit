/*
 * mysqlucsc.cpp
 *
 *  Created on: Oct 2, 2011
 *      Author: lindenb
 */
#include <cstdlib>
#include <vector>
#include <map>
#include <set>
#include <cerrno>
#include <fstream>
#include <string>
#include <cstring>
#include <stdexcept>
#include <climits>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <iostream>
#include <zlib.h>
#include <sstream>
#include <algorithm>
#include <cassert>
#include <stdint.h>
#include <mysql.h>
#define NOWHERE
#include "where.h"
#include "throw.h"
#include "zstreambuf.h"
#include "tokenizer.h"
#include "bin.h"
#include "where.h"
#include "mysqlapplication.h"

using namespace std;


enum	{
    select_any=0,
    select_user_IN_sql=1,
    select_user_OUT_sql=2
};

class Table
    {
    public:
	bool hasBin;
	string name;
	string chrom;
	string chromStart;
	string chromEnd;
	vector<string> columns;
    };

class MysqlUcsc:public MysqlApplication
    {
    public:

	 Table* table;
	 bool first_line_header;
	 int chromcol;
	 int startcol;
	 int endcol;
	 int limit;
	 bool data_are_plus1_based;
	 int select_type;

	 MysqlUcsc():
		 table(NULL),
		 first_line_header(true),
		 limit(-1),
		 data_are_plus1_based(true),
		 select_type(select_any)
	     {
	     chromcol=0;
	     startcol=1;
	     endcol=1;
	     }



	 ~MysqlUcsc()
	     {
	     if(table!=NULL) delete table;
	     }


	 Table* schema(const char* tableName)
	     {
		 WHERE(tableName);
	     Table* table=new Table;
	     table->hasBin=false;
	     table->name.assign(tableName);
	     MYSQL_ROW row;
	     ostringstream os;
	     os << "desc "<< tableName;
	     string query=os.str();

	     mysql_real_query( mysql, query.c_str(),query.size());
	     WHERE(query);
	     MYSQL_RES*	res=mysql_use_result( mysql );
	     if(res==NULL) THROW("mysql_use_result failed");
	     //int num_fields = mysql_num_fields(res);
	     while(( row = mysql_fetch_row( res ))!=NULL )
		     {
	    	 WHERE("");
		     unsigned long *lengths= mysql_fetch_lengths(res);
		     if(lengths==NULL) THROW("cannot fetch length");
		     string colName(row[0],lengths[0]);
		     WHERE(colName);
		     table->columns.push_back(colName);
		     if(colName.compare("bin")==0)
			 {
			 table->hasBin=true;
			 }
		     else if(colName.compare("chrom")==0)
			 {
			 table->chrom=colName;
			 }
		     else if(colName.compare("chromStart")==0)
			 {
			 table->chromStart=colName;
			 }
		     else if(colName.compare("chromEnd")==0)
			 {
			 table->chromEnd=colName;
			 }
		     else if(colName.compare("txStart")==0)
				 {
				 table->chromStart=colName;
				 }
		    else if(colName.compare("txEnd")==0)
				 {
				 table->chromEnd=colName;
				 }
		     }
	     mysql_free_result( res );
	     if(table->chrom.empty())
		 {
		 cerr << "Cannot find chrom column in "<< tableName << endl;
		 delete table;
		 return 0;
		 }
	     if(table->chromStart.empty())
		 {
		 cerr << "Cannot find chromStart column in "<< tableName << endl;
		 delete table;
		 return 0;
		 }
	     if(table->chromEnd.empty())
		 {
		 cerr << "Cannot find chromEnd column in "<< tableName << endl;
		 delete table;
		 return 0;
		 }
	     WHERE(tableName);
	     return table;
	     }

	 void run(std::istream& in)
	     {
	     string line;
	     vector<int> binList;
	     size_t nLine=0;
	     vector<string> tokens;
	     ostringstream os;
	     os << "select ";
	     for(size_t i=0;i<  table->columns.size();++i)
		 {
		 if(i>0) os << ",";
		 os << table->columns[i];
		 }
	     os << " from "<< table->name << " where chrom=\"" ;
	     const string base_query(os.str());

	     while(getline(in,line,'\n'))
			 {
	    	 WHERE(line);
			 ++nLine;
			 tokenizer.split(line,tokens);
			 if(nLine==1 && first_line_header)
				 {
				 for(size_t i=0;i< tokens.size();++i)
				 {
				 if(i>0) cout << tokenizer.delim;
				 cout << tokens[i];
				 }
				 for(size_t i=0;i< table->columns.size();++i)
				 {
				 cout << tokenizer.delim<< table->columns[i];
				 }
				 cout <<endl;
				 continue;
				 }
			 if((size_t)chromcol>=tokens.size())
				 {
				 cerr << "Chrom column: index out of range in "<< line << endl;
				 continue;
				 }
			 if((size_t)startcol>=tokens.size())
				 {
				 cerr << "START column: index out of range in "<< line << endl;
				 continue;
				 }
			 if((size_t)endcol>=tokens.size())
				 {
				 cerr << "END column: index out of range in "<< line << endl;
				 continue;
				 }

			 char* p2;
			 int chromStart=strtol(tokens[startcol].c_str(),&p2,10);
			 if(*p2!=0)
				 {
				 cerr << "Bad chromStart in "<< line << endl;
				 continue;
				 }
			 int chromEnd=strtol(tokens[endcol].c_str(),&p2,10);
			 if(*p2!=0)
				 {
				 cerr << "Bad chromStart in "<< line << endl;
				 continue;
				 }
			 if(data_are_plus1_based)
				 {
				 chromStart--;
				 chromEnd--;
				 }

			 ostringstream sql;
			 sql << base_query;
			 sql << tokens[chromcol] << "\" and ";
			 switch(select_type)
				 {
				 case select_user_IN_sql:
				 {
				 sql << table->chromStart << " <= " << chromStart
					<< " and "<< table->chromEnd << " >= " << chromEnd;

				 break;
				 }
				 case select_user_OUT_sql:
				 {
				 sql << table->chromStart << " >= " << chromStart
					<< " and "<< table->chromEnd << " <= " << chromEnd
					 ;
				 break;
				 }
				 default:
				 {
				 sql << " NOT(" << table->chromEnd << " <= " << chromStart
					<< " or "<< table->chromStart << " > " << chromEnd
					<< ")";
				 break;
				 }
				 }
			 if(table->hasBin)
				 {
				 binList.clear();
				 UcscBin::bins(chromStart,chromEnd,binList);
				 sql << " and bin in (";
				 for(size_t i=0;i< binList.size();++i)
				 {
				 if(i>0) sql << ",";
				 sql << binList[i];
				 }
				 sql << ")";
				 }
			  if(limit>0)
				 {
				 sql << " limit "<< limit;
				 }
			 bool found=false;
			 string query(sql.str());

			 MYSQL_ROW row;
			 if(mysql_real_query( mysql, query.c_str(),query.size())!=0)
				 {
				 cerr << "Failure for "<< query << "\n";
				 cerr << mysql_error(mysql)<< endl;
				 continue;
				 }
			 MYSQL_RES* res=mysql_use_result( mysql );
			 int ncols=mysql_field_count(mysql);
			 while(( row = mysql_fetch_row( res ))!=NULL )
				 {
				 for(size_t i=0;i< tokens.size();++i)
					 {
					 if(i>0) cout << tokenizer.delim;
					 cout << tokens[i];
					 }
				 for(int i=0;i< ncols;++i)
					 {
					 cout << tokenizer.delim << row[i];
					 }
				 cout << endl;
				 found=true;
				 }
			 ::mysql_free_result( res );

			 if(!found)
				 {
				 for(size_t i=0;i< tokens.size();++i)
				 {
				 if(i>0) cout << tokenizer.delim;
				 cout << tokens[i];
				 }
				 for(size_t i=0;i< table->columns.size();++i)
				 {
				 cout << tokenizer.delim ;
				 }
				 cout << endl;
				 }

			 }
	     }

    };

int main(int argc,char** argv)
    {
    MysqlUcsc app;
    int optind=1;
    int n_optind;

    vector<string> custom_fields;
    char* tablename=NULL;

    while(optind < argc)
	    {
	    if(std::strcmp(argv[optind],"-h")==0)
		    {
		    cerr << argv[0] << "Pierre Lindenbaum PHD. 2011.\n";
		    cerr << "Compilation: "<<__DATE__<<"  at "<< __TIME__<<".\n";
		    cerr << "Options:\n";
		    cerr << "  --delim (char) delimiter default:tab\n";
		    cerr << "  --table or -T (string)\n";
		    cerr << "  -C (int) chromosome column (first is 1). default:"<< (app.chromcol+1)<< "\n";
		    cerr << "  -S (int)start column (first is 1). default:"<< (app.startcol+1)<< "\n";
		    cerr << "  -E (int) end column (first is 1). default:"<< (app.endcol+1)<< "\n";
		    cerr << "  -f first column is not header.\n";
		    cerr << "  -1 data are '0' based.\n";
		    cerr << "  --limit (int) limit number or rows returned\n";
		    cerr << "  --field <string> set custom field. Can be used several times\n";
		    cerr << "  --type (int) type of selection: 0 any (default), 1 user data IN mysql data,2 user data embrace mysql data.\n";
		    cerr << "(stdin|files)\n\n";
		    app.printConnectOptions(cerr);
		    exit(EXIT_FAILURE);
		    }
	    else if((n_optind=app.argument(optind,argc,argv))!=-1)
	    	{
	    	if(optind==-2) return EXIT_FAILURE;
	    	optind=n_optind;
	    	}
	    else if(std::strcmp(argv[optind],"--type")==0 && optind+1<argc)
		{
		char* p2;
		app.select_type=(int)strtol(argv[++optind],&p2,10);
		if(app.select_type<0 || app.select_type>2)
		    {
		    cerr << "Bad select type\n";
		    return EXIT_FAILURE;
		    }
		}
	    else if(std::strcmp(argv[optind],"--field")==0 && optind+1<argc)
		{
		custom_fields.push_back(argv[++optind]);
		}
	    else if(std::strcmp(argv[optind],"-1")==0 && optind+1<argc)
		{
		app.data_are_plus1_based=false;
		}
	    else if(std::strcmp(argv[optind],"--limit")==0 && optind+1<argc)
		{
		char* p2;
		app.limit=(int)strtol(argv[++optind],&p2,10);
		if(app.limit<1 || *p2!=0)
		    {
		    cerr << "Bad limit\n";
		    return EXIT_FAILURE;
		    }
		}
	    else if(std::strcmp(argv[optind],"-C")==0 && optind+1<argc)
		{
		char* p2;
		app.chromcol=(int)strtol(argv[++optind],&p2,10);
		if(app.chromcol<1 || *p2!=0)
		    {
		    cerr << "Bad CHROM column.\n";
		    return EXIT_FAILURE;
		    }
		app.chromcol--;
		}
	    else if(std::strcmp(argv[optind],"-S")==0 && optind+1<argc)
		{
		char* p2;
		app.startcol=(int)strtol(argv[++optind],&p2,10);
		if(app.startcol<1 || *p2!=0)
		    {
		    cerr << "Bad START column.\n";
		    return EXIT_FAILURE;
		    }
		app.startcol--;
		}
	    else if(std::strcmp(argv[optind],"-E")==0 && optind+1<argc)
		{
		char* p2;
		app.endcol=(int)strtol(argv[++optind],&p2,10);
		if(app.endcol<1 || *p2!=0)
		    {
		    cerr << "Bad END column.\n";
		    return EXIT_FAILURE;
		    }
		app.endcol--;
		}
	    else if(std::strcmp(argv[optind],"-f")==0 && optind+1<argc)
		{
		app.first_line_header=false;
		}
	    else if((std::strcmp(argv[optind],"--table")==0 || std::strcmp(argv[optind],"-T")==0) && optind+1<argc)
		{
		tablename=(argv[++optind]);
		}
	    else if(std::strcmp(argv[optind],"--delim")==0 && optind+1< argc)
			{
			char* p=argv[++optind];
			if(strlen(p)!=1)
				{
				cerr << "Bad delimiter \""<< p << "\"\n";
				exit(EXIT_FAILURE);
				}
			app.tokenizer.delim=p[0];
			}
	    else if(argv[optind][0]=='-')
			{
			fprintf(stderr,"unknown option '%s'\n",argv[optind]);
			exit(EXIT_FAILURE);
			}
	    else
		{
		break;
		}
	    ++optind;
	       }

    if(tablename==NULL)
	{
	cerr << "undefined table" << endl;
	return EXIT_FAILURE;
	}
    if(app.chromcol<0)
    	{
    	cerr << "undefined CHROM col"<< endl;
    	return EXIT_FAILURE;
    	}
    if(app.startcol<0)
	{
	cerr << "undefined START col"<< endl;
	return EXIT_FAILURE;
	}
    if(app.endcol<0) app.endcol=app.startcol;
    app.connect();
    app.table=app.schema(tablename);
    if(app.table==NULL) THROW("Cannot get schema for "<< tablename);
    if(!custom_fields.empty())
		{
		app.table->columns.clear();
		for(size_t i=0;i< custom_fields.size();++i)
			{
			app.table->columns.push_back(custom_fields[i]);
			}
		}
    if(app.table==NULL)
		{
		cerr << "Cannot get table "<< app.database<<"."<< tablename << endl;
		return EXIT_FAILURE;
		}
    if(optind==argc)
	    {
    	igzstreambuf buf;
		istream in(&buf);
		app.run(in);
	    }
    else
	    {
    	while(optind< argc)
			{
			char* filename=argv[optind++];
			igzstreambuf buf(filename);
			istream in(&buf);
			app.run(in);
			buf.close();
			}
	    }
    return EXIT_SUCCESS;
    }
