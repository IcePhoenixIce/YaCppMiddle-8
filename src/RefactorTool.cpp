#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <functional>
#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

// Рекурсивно проверяем, есть ли у данного класса наследники в текущей единице трансляции.
// Обходим все объявления в AST, ищем классы, у которых в списке базовых классов
// присутствует RD (сравниваем по canonical declaration).
static bool hasDerivedClasses(const CXXRecordDecl *RD, ASTContext &Context) {
    const TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
    std::function<bool(const DeclContext *)> check = [&](const DeclContext *DC) -> bool {
        for (const Decl *D : DC->decls()) {
            if (const CXXRecordDecl *Derived = dyn_cast<CXXRecordDecl>(D)) {
                if (Derived->hasDefinition() && Derived != RD) {
                    for (const CXXBaseSpecifier &Base : Derived->bases()) {
                        const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
                        if (BaseDecl && BaseDecl->getCanonicalDecl() == RD->getCanonicalDecl()) {
                            return true;
                        }
                    }
                }
            }
            if (const DeclContext *NestedDC = dyn_cast<DeclContext>(D)) {
                if (NestedDC != DC && !isa<FunctionDecl>(D)) {
                    if (check(NestedDC))
                        return true;
                }
            }
        }
        return false;
    };
    return check(TU);
}

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;
    auto &LangOpts = Result.Context->getLangOpts();

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(Dtor, Diag, SM, LangOpts, *Result.Context);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride")) {
        handle_miss_override(Method, Diag, SM, LangOpts);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(LoopVar, Diag, SM, LangOpts);
    }
}

// todo: необходимо реализовать обработку случая невиртуального деструктора
void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM,
                                     const LangOptions &LangOpts, ASTContext &Context) {
    // Реализуйте Ваш код ниже
    if (!Dtor || Dtor->isImplicit() || !SM.isInMainFile(Dtor->getLocation()))
        return;

    const CXXRecordDecl *RD = Dtor->getParent();
    if (!RD || !hasDerivedClasses(RD, Context))
        return;

    SourceLocation Loc = Dtor->getLocation();
    unsigned Hash = SM.getFileOffset(Loc);
    if (virtualDtorLocations.count(Hash))
        return;

    Rewrite.InsertTextBefore(Loc, "virtual ");
    virtualDtorLocations.insert(Hash);

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Добавлен virtual к деструктору");
    Diag.Report(Dtor->getLocation(), DiagID);
}

// todo: необходимо реализовать обработку случая отсутствие override
void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM,
                                           const LangOptions &LangOpts) {
    // Реализуйте Ваш код ниже
    if (!Method || Method->hasAttr<OverrideAttr>() || !SM.isInMainFile(Method->getLocation()))
        return;

    SourceLocation NameEnd = Method->getNameInfo().getEndLoc();
    SourceLocation CurrLoc = NameEnd;

    while (CurrLoc.isValid()) {
        Token Tok;
        if (Lexer::getRawToken(CurrLoc, Tok, SM, LangOpts, true)) {
            break;
        }

        if (Tok.getKind() == tok::r_paren) {
            SourceLocation AfterRParen = Lexer::getLocForEndOfToken(Tok.getLocation(), 0, SM, LangOpts);
            Rewrite.InsertTextBefore(AfterRParen, " override");

            const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Добавлен override к методу");
            Diag.Report(Method->getLocation(), DiagID);
            break;
        }

        if (Tok.getKind() == tok::l_brace || Tok.getKind() == tok::semi || Tok.getKind() == tok::colon) {
            break;
        }

        CurrLoc = Lexer::getLocForEndOfToken(Tok.getLocation(), 0, SM, LangOpts);
    }
}

// todo: необходимо реализовать обработку случая отсутствие & в range-for
void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM,
                                        const LangOptions &LangOpts) {
    // Реализуйте Ваш код ниже
    if (!LoopVar || !SM.isInMainFile(LoopVar->getLocation()))
        return;

    TypeLoc TL = LoopVar->getTypeSourceInfo()->getTypeLoc();
    SourceLocation TypeEnd = TL.getEndLoc();
    SourceLocation AfterType = Lexer::getLocForEndOfToken(TypeEnd, 0, SM, LangOpts);
    Rewrite.InsertTextBefore(AfterType, "&");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "Добавлена ссылка к переменной range-for");
    Diag.Report(LoopVar->getLocation(), DiagID);
}

// todo: ниже необходимо реализовать матчеры для поиска узлов AST
// note: синтаксис написания матчеров точно такой же как и для использования clang-query
/*
    Пример того, как может выглядеть реализация:
    auto AllClassesMatcher()
    {
        return cxxRecordDecl().bind("classDecl");
    }
*/
auto NvDtorMatcher() {
    // todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры для поиска невиртуальных
    // деструкторов
    return cxxDestructorDecl(unless(isVirtual()), unless(isImplicit())).bind("nonVirtualDtor");
}

auto NoOverrideMatcher() {
    // todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры для поиска методов без override
    return cxxMethodDecl(
        isOverride(),
        unless(cxxDestructorDecl())).bind("missingOverride");
}

auto NoRefConstVarInRangeLoopMatcher() {
    // todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры для поиска range-for без &
    return cxxForRangeStmt(hasLoopVariable(
        varDecl(hasType(isConstQualified()), unless(hasType(referenceType())), unless(hasType(builtinType())))
            .bind("loopVar")));
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    // Создаем ClangTool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}