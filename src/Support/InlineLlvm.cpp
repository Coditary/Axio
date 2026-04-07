#include "axc/Support/InlineLlvm.h"

#include <cctype>
#include <sstream>

#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

namespace axc {

namespace {

bool isSimpleLlvmIdentifier(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    const auto isHead = [](unsigned char ch) {
        return std::isalpha(ch) || ch == '_' || ch == '$' || ch == '.';
    };
    const auto isTail = [&](unsigned char ch) {
        return isHead(ch) || std::isdigit(ch);
    };
    if (!isHead(static_cast<unsigned char>(value.front()))) {
        return false;
    }
    for (std::size_t i = 1; i < value.size(); ++i) {
        if (!isTail(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return true;
}

std::string llvmGlobalName(const std::string& name) {
    if (isSimpleLlvmIdentifier(name)) {
        return "@" + name;
    }
    std::string escaped;
    escaped.reserve(name.size() + 4);
    escaped += "@\"";
    for (char ch : name) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    escaped += '"';
    return escaped;
}

std::string llvmLocalName(const std::string& name) {
    if (isSimpleLlvmIdentifier(name)) {
        return "%" + name;
    }
    std::string escaped;
    escaped.reserve(name.size() + 4);
    escaped += "%\"";
    for (char ch : name) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    escaped += '"';
    return escaped;
}

bool buildInlineLlvmTypeText(const Type& type, std::string& typeText, std::string& errorMessage) {
    if (type.name == "str" || type.pointerDepth > 0 || !type.modifiers.empty()) {
        typeText = "ptr";
        return true;
    }

    if (!type.arrayExtents.empty()) {
        for (const auto& extent : type.arrayExtents) {
            if (!extent.has_value()) {
                typeText = "ptr";
                return true;
            }
        }

        Type elementType = type;
        elementType.arrayExtents.clear();
        std::string elementText;
        if (!buildInlineLlvmTypeText(elementType, elementText, errorMessage)) {
            return false;
        }

        typeText = elementText;
        for (auto it = type.arrayExtents.rbegin(); it != type.arrayExtents.rend(); ++it) {
            typeText = "[" + std::to_string(**it) + " x " + typeText + "]";
        }
        return true;
    }

    if (type.name.empty() || type.name == "int" || type.name == "i32" || type.name == "error") {
        typeText = "i32";
        return true;
    }
    if (type.name == "void") {
        typeText = "void";
        return true;
    }
    if (type.name == "bool") {
        typeText = "i1";
        return true;
    }
    if (type.name == "char" || type.name == "i8" || type.name == "u8" || type.name == "f8") {
        typeText = "i8";
        return true;
    }
    if (type.name == "i2") {
        typeText = "i2";
        return true;
    }
    if (type.name == "i16" || type.name == "u16" || type.name == "short") {
        typeText = "i16";
        return true;
    }
    if (type.name == "i64" || type.name == "u64" || type.name == "long") {
        typeText = "i64";
        return true;
    }
    if (type.name == "float" || type.name == "f32") {
        typeText = "float";
        return true;
    }
    if (type.name == "double" || type.name == "f64") {
        typeText = "double";
        return true;
    }
    if (type.name == "f16") {
        typeText = "half";
        return true;
    }

    errorMessage = "inline llvm functions currently only support builtin scalar and pointer-like types";
    return false;
}

std::string formatLlvmDiagnostic(const llvm::SMDiagnostic& diagnostic) {
    std::ostringstream out;
    if (diagnostic.getLineNo() > 0 && diagnostic.getColumnNo() > 0) {
        out << "line " << diagnostic.getLineNo() << ", column " << diagnostic.getColumnNo() << ": ";
    }
    out << diagnostic.getMessage().str();
    return out.str();
}

}  // namespace

bool buildInlineLlvmModuleText(const FunctionDecl& function,
                               const std::string& functionName,
                               std::string& moduleText,
                               std::string& errorMessage) {
    moduleText.clear();
    errorMessage.clear();

    std::string returnTypeText;
    if (function.returnsVoid()) {
        returnTypeText = "void";
    } else if (function.returnTypes.size() == 1) {
        if (!buildInlineLlvmTypeText(function.returnTypes.front(), returnTypeText, errorMessage)) {
            return false;
        }
    } else {
        errorMessage = "inline llvm functions currently support at most one return value";
        return false;
    }

    std::ostringstream out;
    out << "define " << returnTypeText << ' ' << llvmGlobalName(functionName) << '(';
    bool first = true;
    for (const auto& parameter : function.compileParameters) {
        std::string typeText;
        if (!buildInlineLlvmTypeText(parameter.type, typeText, errorMessage)) {
            return false;
        }
        if (!first) {
            out << ", ";
        }
        out << typeText << ' ' << llvmLocalName(parameter.name);
        first = false;
    }
    for (const auto& parameter : function.runtimeParameters) {
        std::string typeText;
        if (!buildInlineLlvmTypeText(parameter.type, typeText, errorMessage)) {
            return false;
        }
        if (!first) {
            out << ", ";
        }
        out << typeText << ' ' << llvmLocalName(parameter.name);
        first = false;
    }
    out << ") {\n";
    out << function.llvmBody;
    if (!function.llvmBody.empty() && function.llvmBody.back() != '\n') {
        out << '\n';
    }
    out << "}\n";
    moduleText = out.str();
    return true;
}

bool verifyInlineLlvmModuleText(const std::string& moduleText,
                                const std::string& functionName,
                                std::string& errorMessage) {
    errorMessage.clear();
    llvm::LLVMContext context;
    llvm::SMDiagnostic parseError;
    std::unique_ptr<llvm::Module> module = llvm::parseAssemblyString(moduleText, parseError, context);
    if (!module) {
        errorMessage = formatLlvmDiagnostic(parseError);
        return false;
    }

    if (module->getFunction(functionName) == nullptr) {
        errorMessage = "inline llvm body did not define the expected function";
        return false;
    }

    std::string verifierOutput;
    llvm::raw_string_ostream verifierStream(verifierOutput);
    if (llvm::verifyModule(*module, &verifierStream)) {
        verifierStream.flush();
        errorMessage = verifierOutput;
        if (!errorMessage.empty() && errorMessage.back() == '\n') {
            errorMessage.pop_back();
        }
        return false;
    }

    return true;
}

}  // namespace axc
