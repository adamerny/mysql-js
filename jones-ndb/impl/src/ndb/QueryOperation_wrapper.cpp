/*
 Copyright (c) 2015, Oracle and/or its affiliates. All rights
 reserved.
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; version 2 of
 the License.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 02110-1301  USA
*/

#include "JsWrapper.h"
#include "js_wrapper_macros.h"
#include "QueryOperation.h"
#include "ndb_util/NdbQueryOperation.hpp"
#include "node_buffer.h"

using namespace v8;


Handle<String>    /* keys of NdbProjection */
  K_next,
  K_root,
  K_hasScan,
  K_keyFields,
  K_joinTo,
  K_depth,
  K_ndbQueryDef,
  K_tableHandler,
  K_rowRecord,
  K_rowBuffer,
  K_indexHandler,
  K_keyRecord,
  K_isPrimaryKey,
  K_dbTable,
  K_dbIndex;


class QueryOperationEnvelopeClass : public Envelope {
public:
  QueryOperationEnvelopeClass() : Envelope("QueryOperation") {
  }
};

QueryOperationEnvelopeClass QueryOperationEnvelope;

Handle<Value> QueryOperation_Wrapper(QueryOperation *queryOp) {
  HandleScope scope;

  if(queryOp) {
    Local<Object> jsobj = QueryOperationEnvelope.newWrapper();
    wrapPointerInObject(queryOp, QueryOperationEnvelope, jsobj);
    freeFromGC(queryOp, jsobj);
    return scope.Close(jsobj);
  }
  return Null();
}

const NdbQueryOperationDef * createTopLevelQuery(QueryOperation *queryOp,
                                                 Handle<Object> spec,
                                                 Handle<Object> keyBuffer) {
  DEBUG_ENTER();
  NdbQueryBuilder *builder = queryOp->getBuilder();

  /* Pull values out of the JavaScript object */
  Local<Value> v;
  const Record * keyRecord = 0;
  const NdbDictionary::Table * table = 0;
  const NdbDictionary::Index * index = 0;

  v = spec->Get(K_keyRecord);
  if(v->IsObject()) {
    keyRecord = unwrapPointer<const Record *>(v->ToObject());
  };
  v = spec->Get(K_tableHandler);
  if(v->IsObject()) {
    v = v->ToObject()->Get(K_dbTable);
    if(v->IsObject()) {
      table = unwrapPointer<const NdbDictionary::Table *>(v->ToObject());
    }
  }
  bool isPrimaryKey = spec->Get(K_isPrimaryKey)->BooleanValue();
  const char * key_buffer = node::Buffer::Data(keyBuffer);
  if(! isPrimaryKey) {
    v = spec->Get(K_indexHandler);
    if(v->IsObject()) {
      v = v->ToObject()->Get(K_dbIndex);
      if(v->IsObject()) {
        index = unwrapPointer<const NdbDictionary::Index *> (v->ToObject());
      }
    }
    assert(index);
  }

  /* Build the key */
  int nKeyParts = keyRecord->getNoOfColumns();
  const NdbQueryOperand * key_parts[nKeyParts+1];

  for(int i = 0; i < nKeyParts ; i++) {
    size_t offset = keyRecord->getColumnOffset(i);
    size_t length = keyRecord->getValueLength(i, key_buffer + offset);
    offset += keyRecord->getValueOffset(i);  // accounts for length bytes
    key_parts[i] = builder->constValue(key_buffer + offset, length);
  }
  key_parts[nKeyParts] = 0;

  return queryOp->defineOperation(index, table, key_parts);
}

const NdbQueryOperationDef * createNextLevel(QueryOperation *queryOp,
                                             Handle<Object> spec,
                                             const NdbQueryOperationDef * parent) {
  NdbQueryBuilder *builder = queryOp->getBuilder();

  /* Pull values out of the JavaScript object */
  Local<Value> v;
  const NdbDictionary::Table * table = 0;
  const NdbDictionary::Index * index = 0;
  int depth = spec->Get(K_depth)->Int32Value();
  DEBUG_PRINT("Creating QueryOperationDef at level %d",depth);

  v = spec->Get(K_tableHandler);
  if(v->IsObject()) {
    v = v->ToObject()->Get(K_dbTable);
    if(v->IsObject()) {
      table = unwrapPointer<const NdbDictionary::Table *>(v->ToObject());
    }
  }
  bool isPrimaryKey = spec->Get(K_isPrimaryKey)->BooleanValue();

  if(! isPrimaryKey) {
    v = spec->Get(K_indexHandler);
    if(v->IsObject()) {
      v = v->ToObject()->Get(K_dbIndex);
      if(v->IsObject()) {
        index = unwrapPointer<const NdbDictionary::Index *> (v->ToObject());
      }
    }
    assert(index);
  }

  v = spec->Get(K_joinTo);
  Local<Array> joinColumns = Array::Cast(*v);

  /* Build the key */
  int nKeyParts = joinColumns->Length();
  const NdbQueryOperand * key_parts[nKeyParts+1];

  for(int i = 0 ; i < nKeyParts ; i++) {
    String::AsciiValue column_name(joinColumns->Get(i));
    key_parts[i] = builder->linkedValue(parent, *column_name);
  }
  key_parts[nKeyParts] = 0;

  return queryOp->defineOperation(index, table, key_parts);
}


Handle<Value> createQueryOperation(const Arguments & args) {
  DEBUG_MARKER(UDEB_DEBUG);
  QueryOperation * queryOperation = new QueryOperation();
  const NdbQueryOperationDef * root, * current;

  Local<Value> v;
  Local<Object> spec = args[0]->ToObject();

  current = root = createTopLevelQuery(queryOperation, spec,
                                       args[1]->ToObject());
  assert(current->getOpNo() == spec->Get(K_depth)->Uint32Value());

  while(! (v = spec->Get(K_next))->IsNull()) {
    spec = v->ToObject();
    current = createNextLevel(queryOperation, spec, current);
    assert(current->getOpNo() == spec->Get(K_depth)->Uint32Value());
  }
  queryOperation->prepare(root);
  return QueryOperation_Wrapper(queryOperation);
}


#define JSSTRING(a) Persistent<String>::New(String::NewSymbol(a))

void QueryOperation_initOnLoad(Handle<Object> target) {
  HandleScope scope;

  Persistent<Object> ibObj = Persistent<Object>(Object::New());
  Persistent<String> ibKey = Persistent<String>(String::NewSymbol("QueryOperation"));
  target->Set(ibKey, ibObj);

  DEFINE_JS_FUNCTION(ibObj, "create", createQueryOperation);

  K_next          = JSSTRING("next");
  K_root          = JSSTRING("root");
  K_hasScan       = JSSTRING("hasScan");
  K_keyFields     = JSSTRING("keyFields");
  K_joinTo        = JSSTRING("joinTo");
  K_depth         = JSSTRING("depth");
  K_ndbQueryDef   = JSSTRING("ndbQueryDef");
  K_tableHandler  = JSSTRING("tableHandler");
  K_rowRecord     = JSSTRING("rowRecord"),
  K_rowBuffer     = JSSTRING("rowBuffer"),
  K_indexHandler  = JSSTRING("indexHandler");
  K_keyRecord     = JSSTRING("keyRecord");
  K_isPrimaryKey  = JSSTRING("isPrimaryKey");

  K_dbTable       = JSSTRING("dbTable");
  K_dbIndex       = JSSTRING("dbIndex");
}

