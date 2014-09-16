#include "formatexpr.h"
#include "parser/ast/sqliteexpr.h"
#include "parser/ast/sqlitecolumntype.h"
#include "parser/ast/sqliteselect.h"
#include "parser/ast/sqliteraise.h"

FormatExpr::FormatExpr(SqliteExpr* expr) :
    expr(expr)
{
}

void FormatExpr::formatInternal()
{
    switch (expr->mode)
    {
        case SqliteExpr::Mode::null:
            break;
        case SqliteExpr::Mode::LITERAL_VALUE:
        {
            if (expr->literalNull)
                withKeyword("NULL");
            else
                withLiteral(expr->literalValue);
            break;
        }
        case SqliteExpr::Mode::CTIME:
            withKeyword(expr->ctime.toUpper());
            break;
        case SqliteExpr::Mode::BIND_PARAM:
            withBindParam(expr->bindParam);
            break;
        case SqliteExpr::Mode::ID:
        {
            if (!expr->database.isNull())
                withId(expr->database).withIdDot();

            if (!expr->table.isNull())
                withId(expr->table).withIdDot();

            withId(expr->column);
            break;
        }
        case SqliteExpr::Mode::UNARY_OP:
            withOperator(expr->unaryOp).withStatement(expr->expr1, "unaryOp");
            break;
        case SqliteExpr::Mode::BINARY_OP:
            withStatement(expr->expr1).withOperator(expr->binaryOp).withStatement(expr->expr2, "binaryOp");
            break;
        case SqliteExpr::Mode::FUNCTION:
            withFuncId(expr->function).withParFuncLeft().withStatementList(expr->exprList, "funcArgs").withParFuncRight();
            break;
        case SqliteExpr::Mode::SUB_EXPR:
            withParExprLeft().incrIndent().withStatement(expr->expr1).withParExprRight();
            break;
        case SqliteExpr::Mode::CAST:
            withKeyword("CAST").withParExprLeft().incrIndent().withStatement(expr->expr1).withKeyword("AS")
                    .withStatement(expr->columnType, "colType").decrIndent().withParExprRight();
            break;
        case SqliteExpr::Mode::COLLATE:
            withStatement(expr->expr1).withKeyword("COLLATE").withId(expr->collation);
            break;
        case SqliteExpr::Mode::LIKE:
        {
            withStatement(expr->expr1);
            if (expr->notKw)
                withKeyword("NOT");

            withKeyword(SqliteExpr::likeOp(expr->like)).withStatement(expr->expr2, "like");

            if (expr->expr3)
                withKeyword("ESCAPE").withStatement(expr->expr3, "likeEscape");

            break;
        }
        case SqliteExpr::Mode::NULL_:
            withKeyword("NULL");
            break;
        case SqliteExpr::Mode::NOTNULL:
        {
            switch (expr->notNull)
            {
                case SqliteExpr::NotNull::ISNULL:
                    withKeyword("ISNULL");
                    break;
                case SqliteExpr::NotNull::NOT_NULL:
                    withKeyword("NOT").withKeyword("NULL");
                    break;
                case SqliteExpr::NotNull::NOTNULL:
                    withKeyword("NOTNULL");
                    break;
                case SqliteExpr::NotNull::null:
                    break;
            }
            break;
        }
        case SqliteExpr::Mode::IS:
        {
            withStatement(expr->expr1).withKeyword("IS");
            if (expr->notKw)
                withKeyword("NOT");

            withStatement(expr->expr2, "is");
            break;
        }
        case SqliteExpr::Mode::BETWEEN:
        {
            withStatement(expr->expr1);

            if (expr->notKw)
                withKeyword("NOT");

            withKeyword("BETWEEN").withStatement(expr->expr2, "between1").withKeyword("AND").withStatement(expr->expr3, "between2");
            break;
        }
        case SqliteExpr::Mode::IN:
        {
            withStatement(expr->expr1);

            if (expr->notKw)
                withKeyword("NOT");

            withKeyword("IN");
            if (expr->select)
            {
                withParDefLeft().incrIndent().withStatement(expr->select).decrIndent().withParDefRight();
            }
            else if (expr->exprList.size() > 0)
            {
                withParExprLeft().incrIndent().withStatementList(expr->exprList).decrIndent().withParExprRight();
            }
            else
            {
                if (!expr->database.isNull())
                    withId(expr->database).withIdDot();

                withId(expr->table);
            }
            break;
        }
        case SqliteExpr::Mode::EXISTS:
            withKeyword("EXISTS").withParDefLeft().incrIndent().withStatement(expr->select).decrIndent().withParDefRight();
            break;
        case SqliteExpr::Mode::CASE:
        {
            withKeyword("CASE");
            if (expr->expr1)
                withStatement(expr->expr1, "case");

            bool then = false;
            foreach (SqliteExpr* expr, expr->exprList)
            {
                if (then)
                    withKeyword("THEN");
                else
                    withKeyword("WHEN");

                incrIndent("case");
                withStatement(expr);
                decrIndent();

                then = !then;
            }

            if (expr->expr2)
                withKeyword("ELSE").incrIndent("case").withStatement(expr->expr2).decrIndent();

            withKeyword("END");
            break;
        }
        case SqliteExpr::Mode::SUB_SELECT:
            withParDefLeft().incrIndent().withStatement(expr->select).decrIndent().withParDefRight();
            break;
        case SqliteExpr::Mode::RAISE:
            withStatement(expr->raiseFunction);
            break;
    }
}
