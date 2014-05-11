#include "queryexecutorexecute.h"
#include "db/sqlerrorcodes.h"
#include "db/queryexecutor.h"
#include "parser/ast/sqlitequery.h"
#include "parser/lexer.h"
#include "parser/ast/sqlitecreatetable.h"
#include "datatype.h"
#include <QDateTime>
#include <QDebug>
#include <schemaresolver.h>

bool QueryExecutorExecute::exec()
{
//    qDebug() << "q:" << context->processedQuery;

    startTime = QDateTime::currentMSecsSinceEpoch();
    return executeQueries();
}

void QueryExecutorExecute::provideResultColumns(SqlQueryPtr results)
{
    QueryExecutor::ResultColumnPtr resCol = QueryExecutor::ResultColumnPtr::create();
    foreach (const QString& colName, results->getColumnNames())
    {
        resCol->displayName = colName;
        context->resultColumns << resCol;
    }
}

bool QueryExecutorExecute::executeQueries()
{
    QHash<QString, QVariant> bindParamsForQuery;
    SqlQueryPtr results;

    Db::Flags flags;
    if (context->preloadResults)
        flags |= Db::Flag::PRELOAD;

    int queryCount = context->parsedQueries.size();
    for (const SqliteQueryPtr& query : context->parsedQueries)
    {
        bindParamsForQuery = getBindParamsForQuery(query);
        results = db->prepare(query->detokenize());
        results->setArgs(bindParamsForQuery);
        results->setFlags(flags);

        queryCount--;
        if (queryCount == 0) // last query?
            setupSqlite2ColumnDataTypes(results);

        results->execute();

        if (results->isError())
        {
            handleFailResult(results);
            return false;
        }
    }
    handleSuccessfulResult(results);
    return true;
}

void QueryExecutorExecute::handleSuccessfulResult(SqlQueryPtr results)
{
    SqliteSelectPtr select = getSelect();
    if (!select || select->coreSelects.size() > 1 || select->explain)
    {
        // In this case, the "Columns" step didn't provide result columns.
        // We need to do it here, basing on actual results.
        provideResultColumns(results);
    }

    context->executionTime = QDateTime::currentMSecsSinceEpoch() - startTime;
    context->rowsAffected = results->rowsAffected();

    // For PRAGMA and EXPLAIN we simply count results for rows returned
    SqliteQueryPtr lastQuery = context->parsedQueries.last();
    if (lastQuery->queryType != SqliteQueryType::Select || lastQuery->explain)
        context->rowsCountingRequired = true;

    if (context->resultsHandler)
    {
        context->resultsHandler(results);
        context->resultsHandler = nullptr;
    }

    context->executionResults = results;
}

void QueryExecutorExecute::handleFailResult(SqlQueryPtr results)
{
    if (!results->isInterrupted())
    {
        qWarning() << "Could not execute query with smart method:" << queryExecutor->getOriginalQuery()
                   << "\nError message:" << results->getErrorText()
                   << "\nSkipping smart execution.";
    }
}

QHash<QString, QVariant> QueryExecutorExecute::getBindParamsForQuery(SqliteQueryPtr query)
{
    QHash<QString, QVariant> queryParams;
    QStringList bindParams = query->tokens.filter(Token::BIND_PARAM).toStringList();
    foreach (const QString& bindParam, bindParams)
    {
        if (context->queryParameters.contains(bindParam))
            queryParams.insert(bindParam, context->queryParameters[bindParam]);
    }
    return queryParams;
}

void QueryExecutorExecute::setupSqlite2ColumnDataTypes(SqlQueryPtr results)
{
    Sqlite2ColumnDataTypeHelper* sqlite2Helper = dynamic_cast<Sqlite2ColumnDataTypeHelper*>(results.data());
    if (!sqlite2Helper)
        return;

    QPair<QString,QString> key;
    SqliteCreateTablePtr createTable;

    SchemaResolver resolver(db);
    QHash<QPair<QString,QString>,SqliteCreateTablePtr> tables;
    for (QueryExecutor::SourceTablePtr tab : context->sourceTables)
    {
        if (tab->table.isNull())
            continue;

        key = QPair<QString,QString>(tab->database, tab->table);
        createTable = resolver.getParsedObject(tab->database, tab->table).dynamicCast<SqliteCreateTable>();
        tables[key] = createTable;
    }

    sqlite2Helper->clearBinaryTypes();

    SqliteCreateTable::Column* column;
    int idx = -1;
    for (QueryExecutor::ResultColumnPtr resCol : context->resultColumns)
    {
        idx++;
        key = QPair<QString,QString>(resCol->database, resCol->table);
        if (!tables.contains(key))
            continue;

        column = tables[key]->getColumn(resCol->column);
        if (column->type && DataType::isBinary(column->type->name))
            sqlite2Helper->setBinaryType(idx);
    }
}
