#include <sqlite3.h>
#include <expat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "list.h"
#include "common_defs.h"

sqlite3 *conn;

#define BUFFSIZE        8192
#define BUF_INCREMENT   BUFFSIZE

struct setinfo{
};

struct setlist;
struct setlist{
	struct setlist *next;
	char name[50];
	char date[20];
};

struct xmldata{
	char *buf;
	long bufsize;
	long bufalloc;
	struct setlist *setlist;

	int cid;
	char cname[50];
	char cset[50];
	char crar[50];

	char typename[50];

	char rhint[255];

	char cost[120];

	int pwr;
	int tgh;
};

static void XMLCALL
chardata(void *arg, const char *s, const int len){
	struct xmldata *data=(struct xmldata*)arg;

	if(*s=='\n' || *s==' '){
		return;
	}

	if(data->bufsize+len>data->bufalloc){
		data->bufalloc=data->bufsize+len+BUF_INCREMENT;
		data->buf=realloc(data->buf,sizeof(*data->buf)*data->bufalloc);
		if(data->buf==NULL){
			fprintf(stderr,"Realloc failed\n");
		}
	}

	memcpy(data->buf+data->bufsize,s,len);

	data->bufsize+=len;
	data->buf[data->bufsize]=0;
}

static void XMLCALL
startset(void *arg, const char *el, const char **attr)
{
	struct xmldata *data=(struct xmldata*)arg;
	if(strcmp(el,"set")==0)
		addNode((void**)&data->setlist,sizeof(*data->setlist));
}

static void XMLCALL
endset(void *arg, const char *el) {
	struct xmldata *data=(struct xmldata*)arg;
	int i,j;

	if(strcmp(el,"name")==0){
		strcpy(data->setlist->name,data->buf);
	}
	if(strcmp(el,"release-date")==0){
		strcpy(data->setlist->date,data->buf);
	}
	data->bufsize=0;
}

static void XMLCALL
startmeta(void *arg, const char *el, const char **attr)
{
	struct xmldata *data=(struct xmldata*)arg;
	int i,j;

	if(strcmp(el,"card")==0){
		for (i = 0; attr[i]; i += 2) {
			if(strcmp(attr[i],"name")==0){
				char query[255];
				sqlite3_stmt *s;
				strcpy(data->cname,attr[i+1]);

				sprintf(query,"INSERT INTO BasicCard(Name) VALUES (?)");
				if(sqlite3_prepare_v2(conn,query,255,&s,NULL)!=SQLITE_OK)exit(2);
				if(sqlite3_bind_text(s,1,data->cname,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
				if(sqlite3_step(s)!=SQLITE_DONE)exit(2);
				if(sqlite3_finalize(s)!=SQLITE_OK)exit(2);
				
				data->cid=sqlite3_last_insert_rowid(conn);

			}
		}
	}
}

static void XMLCALL
endmeta(void *arg, const char *el) {
	struct xmldata *data=(struct xmldata*)arg;
	int i,j;

	if(strcmp(el,"set")==0){
		strcpy(data->cset,data->buf);
	}
	if(strcmp(el,"rarity")==0){
		strcpy(data->crar,data->buf);
	}
	if(strcmp(el,"instance")==0){
		char query[255];
		sqlite3_stmt *s;
		int rint;

		switch(data->crar[0]){
			case 'C':
				rint=1;break;
			case 'U':
				rint=2;break;
			case 'R':
				rint=3;break;
			case 'M':
				rint=4;break;
			default:
				rint=0;break;
		}

		sprintf(query,"INSERT INTO CardSet(SetID,CardID,Rarity) SELECT ID,?,? FROM SetInfo WHERE Name=?");
		if(sqlite3_prepare_v2(conn,query,255,&s,NULL)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_int(s,1,data->cid)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_int(s,2,rint)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_text(s,3,data->cset,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
		if(sqlite3_step(s)!=SQLITE_DONE)exit(2);
		if(sqlite3_finalize(s)!=SQLITE_OK)exit(2);
	}
	data->bufsize=0;
}

static void XMLCALL
startcard(void *arg, const char *el, const char **attr)
{
	struct xmldata *data=(struct xmldata*)arg;
	int i,j;

	if(strcmp(el,"card")==0){
		data->pwr=data->tgh=0;
		*data->cost=0;
	}
	if(strcmp(el,"type")==0){
		for (i = 0; attr[i]; i += 2) {
			if(strcmp(attr[i],"type")==0){
				switch(attr[i+1][2]){
					case 'r': sprintf(data->typename,"TypeCard");break;
					case 'b': sprintf(data->typename,"TypeSub");break;
					case 'p': sprintf(data->typename,"TypeSuper");break;
				}
			}
		}
	}
	if(strcmp(el,"rule")==0){
		*data->rhint=0;
		for (i = 0; attr[i]; i += 2) {
			if(strcmp(attr[i],"reminder")==0){
				strcpy(data->rhint,attr[i+1]);
			}
		}
	}
}

static void XMLCALL
endcard(void *arg, const char *el) {
	struct xmldata *data=(struct xmldata*)arg;
	int i,j;

	if(strcmp(el,"name")==0){
		strcpy(data->cname,data->buf);
	}
	if(strcmp(el,"type")==0){
		char query[255];
		sqlite3_stmt *s;
		sprintf(query,"UPDATE BasicCard SET %s=? WHERE Name=?",data->typename);
		if(sqlite3_prepare_v2(conn,query,255,&s,NULL)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_text(s,1,data->buf,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_text(s,2,data->cname,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
		if(sqlite3_step(s)!=SQLITE_DONE)exit(2);
		if(sqlite3_finalize(s)!=SQLITE_OK)exit(2);
	}
	if(strcmp(el,"rule")==0){
		char query[255];
		sqlite3_stmt *s;
		sprintf(query,"INSERT INTO CardRule(CardID,Data,Hint) SELECT ID,?,? FROM BasicCard WHERE Name=?");
		if(sqlite3_prepare_v2(conn,query,255,&s,NULL)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_text(s,1,data->buf,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_text(s,2,data->rhint,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_text(s,3,data->cname,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
		if(sqlite3_step(s)!=SQLITE_DONE)exit(2);
		if(sqlite3_finalize(s)!=SQLITE_OK)exit(2);
	}
	if(strcmp(el,"pow")==0){
		data->pwr=strtol(data->buf,NULL,10);
	}
	if(strcmp(el,"tgh")==0){
		data->tgh=strtol(data->buf,NULL,10);
	}
	if(strcmp(el,"cost")==0){
		strcpy(data->cost,data->buf);
	}
	if(strcmp(el,"card")==0){
		char query[255];
		sqlite3_stmt *s;

		sprintf(query,"UPDATE BasicCard SET Cost=?, Pwr=?, Tgh=? WHERE Name=?");
		if(sqlite3_prepare_v2(conn,query,255,&s,NULL)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_text(s,1,data->cost,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_int(s,2,data->pwr)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_int(s,3,data->tgh)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_text(s,4,data->cname,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
		if(sqlite3_step(s)!=SQLITE_DONE)exit(2);
		if(sqlite3_finalize(s)!=SQLITE_OK)exit(2);
	}
	data->bufsize=0;
}

int run_xml_parse(FILE *fd, XML_Parser p){
	int ret=0;
	char buff[BUFFSIZE];
	for (;;) {
		int done;
		int len;

		len = (int)fread(buff, 1, BUFFSIZE, fd);
		if (ferror(fd)) {
			fprintf(stderr, "Read error\n");
			exit(-1);
		}
		done = feof(fd);

		if (XML_Parse(p, buff, len, done) == XML_STATUS_ERROR) {
			fprintf(stderr, "Parse error at line %lu:\n%s\n",
					XML_GetCurrentLineNumber(p),
					XML_ErrorString(XML_GetErrorCode(p)));
			ret=1;
			break;
		}

		if (done)
			break;
	}

	return ret;
}

void fill_sets(struct setlist *list){
	char query[255];
	sqlite3_stmt *s;

	sprintf(query,"INSERT INTO SetInfo(Name) VALUES (?)");
	while(list){
		if(sqlite3_prepare_v2(conn,query,255,&s,NULL)!=SQLITE_OK)exit(2);
		if(sqlite3_bind_text(s,1,list->name,-1,SQLITE_STATIC)!=SQLITE_OK)exit(2);
		if(sqlite3_step(s)!=SQLITE_DONE)exit(2);
		if(sqlite3_finalize(s)!=SQLITE_OK)exit(2);
		list=list->next;
	}
}

int main(int argc, char **argv){
	struct xmldata data;
	FILE *fd;
	int ret=0;
	int numscan=0;
	int i;
	data.setlist=NULL;

	sqlite3_open_v2("mtg.db",&conn,SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE,NULL);

	XML_Parser p = XML_ParserCreate(NULL);
	if (! p) {
		fprintf(stderr, "Couldn't allocate memory for parser\n");
		exit(-1);
	}

	data.bufsize=0;
	data.bufalloc=BUFFSIZE;
	//data.buf=malloc(sizeof(*data.buf)*data.bufalloc);
	INIT_MEM(data.buf,data.bufalloc);

	XML_SetUserData(p,&data);
	XML_SetElementHandler(p, startset, endset);
	XML_SetCharacterDataHandler(p,chardata);

	fd=fopen(argv[1],"r"); //setinfo
	if(fd==NULL){
		fprintf(stderr,"Couldn't open file\n");
		exit(-1);
	}

	run_xml_parse(fd,p);
	fclose(fd);

	fill_sets(data.setlist);
fprintf(stderr,"sets done\n");

	XML_ParserFree(p);
	p = XML_ParserCreate(NULL);
	if (! p) {
		fprintf(stderr, "Couldn't allocate memory for parser\n");
		exit(-1);
	}
	XML_SetUserData(p,&data);
	XML_SetElementHandler(p, startmeta, endmeta);
	XML_SetCharacterDataHandler(p,chardata);
	fd=fopen(argv[2],"r"); // meta
	if(fd==NULL){
		fprintf(stderr,"Couldn't open file\n");
		exit(-1);
	}

	run_xml_parse(fd,p);
fprintf(stderr,"meta done\n");

	XML_ParserFree(p);
	fclose(fd);

	p = XML_ParserCreate(NULL);
	if (! p) {
		fprintf(stderr, "Couldn't allocate memory for parser\n");
		exit(-1);
	}

	XML_SetUserData(p,&data);
	XML_SetElementHandler(p, startcard, endcard);
	XML_SetCharacterDataHandler(p,chardata);
	fd=fopen(argv[3],"r"); // meta
	if(fd==NULL){
		fprintf(stderr,"Couldn't open file\n");
		exit(-1);
	}

	run_xml_parse(fd,p);
fprintf(stderr,"card done\n");

	XML_ParserFree(p);
	fclose(fd);
	free(data.buf);

	return 0;
}
