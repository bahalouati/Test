#!/usr/bin/env python3
"""qtestgen -- turn a C++ function declaration into a Qt Test skeleton.

Feed it a signature (or a header and a function name) and it writes the part of
a QTestLib test that is mechanical: the slot pair, the data columns typed from
the parameters, the QFETCH lines, the call, and the comparison. What it cannot
write is the part that matters -- which inputs are interesting and what the
answer should be. Every generated body therefore fails until a human edits it,
rather than passing vacuously.

    # a one-off, straight from a signature
    tools/qtestgen.py "QString formatDuration(int seconds)"

    # from the declaration in a header, so types resolve and structs expand
    tools/qtestgen.py --header src/core/jiratypes.h --function formatDuration

    # what can it see?
    tools/qtestgen.py --header src/core/jiratypes.h --list

    # a whole test file, with the CMake lines to build it
    tools/qtestgen.py --header src/core/tasktracker.h --all --standalone

    # or splice slots into the suite that already exists
    tools/qtestgen.py --header src/core/jiratypes.h --function issueSortKey \
        --insert tests/tst_jiracore.cpp

Limits, up front: this reads declarations, not C++. It understands namespaces,
structs, member and static member functions, references, pointers, default
arguments and template-argument commas. It skips templates, operators,
constructors, macros and anything hiding behind the preprocessor. A full parse
means libclang; this deliberately stays a script with no dependencies.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# Types QTest::addColumn<T>() already accepts: Qt registers these itself, so no
# Q_DECLARE_METATYPE is needed for them.
QT_VALUE_TYPES = {
    "bool", "char", "signed char", "unsigned char", "short", "unsigned short",
    "int", "unsigned", "unsigned int", "long", "unsigned long", "long long",
    "unsigned long long", "float", "double", "qreal", "qint8", "quint8",
    "qint16", "quint16", "qint32", "quint32", "qint64", "quint64", "qsizetype",
    "size_t", "QString", "QByteArray", "QStringList", "QChar", "QDate", "QTime",
    "QDateTime", "QUrl", "QVariant", "QVariantList", "QVariantMap", "QJsonValue",
    "QJsonObject", "QJsonArray", "QJsonDocument", "QPoint", "QPointF", "QSize",
    "QSizeF", "QRect", "QRectF", "QColor", "QFont", "QLocale", "QUuid",
}

# Qt declares metatypes for these itself; saying so again is a redefinition,
# not a no-op. They are not necessarily constructible on the stack.
SELF_DECLARED_METATYPES = {
    "QNetworkRequest", "QNetworkReply", "QNetworkCookie", "QNetworkProxy",
    "QSslConfiguration", "QSslError", "QSslCertificate", "QHostAddress",
    "QModelIndex", "QPersistentModelIndex", "QFileInfo", "QDir", "QTextCursor",
}

KNOWN_METATYPES = QT_VALUE_TYPES | SELF_DECLARED_METATYPES

INTEGER_TYPES = {
    "char", "signed char", "unsigned char", "short", "unsigned short", "int",
    "unsigned", "unsigned int", "long", "unsigned long", "long long",
    "unsigned long long", "qint8", "quint8", "qint16", "quint16", "qint32",
    "quint32", "qint64", "quint64", "qsizetype", "size_t",
}

FLOAT_TYPES = {"float", "double", "qreal"}

TYPE_KEYWORDS = {
    "int", "long", "short", "char", "unsigned", "signed", "float", "double",
    "bool", "void", "const", "volatile", "auto",
}

# Leading words that are already third person, so pluralising them reads wrong
# ("cans", "hases"). The repo names its slots as sentences -- formatsDurations,
# parsesComments -- and this keeps that habit without mangling predicates.
NON_VERB_PREFIXES = {
    "is", "has", "can", "should", "must", "was", "were", "are", "be", "does", "did",
    # Prepositions, which are not verbs: fromJson must not become fromsJson.
    "from", "to", "of", "with", "without", "for", "as", "at", "by", "in", "on",
}

ACCESS_PATTERN = re.compile(r"(^|\s)(?P<label>public|private|protected|signals|Q_SIGNALS)(\s+(slots|Q_SLOTS))?$")

EXAMPLES = """examples:
  qtestgen.py "QString formatDuration(int seconds)"
  qtestgen.py --header src/core/jiratypes.h --list
  qtestgen.py --header src/core/jiratypes.h --function formatDuration
  qtestgen.py --header src/core/tasktracker.h --all --standalone
  qtestgen.py --header src/core/jiratypes.h --function issueSortKey --insert tests/tst_jiracore.cpp

Give it the header rather than the signature where you can: reading the header
is what lets it qualify types, expand a returned struct into one expectation
column per field, skip private helpers, and build a fixture for a class whose
constructor takes arguments.

A generated case fails until someone writes it -- what the answer should be is
the part no tool can supply. See the README for why that is deliberate."""

# Row names the generated test recognises as "nobody has filled this in yet".
PLACEHOLDER_TAG = "TODO"

DECL_SKIP = re.compile(r"\b(template|operator|Q_OBJECT|Q_PROPERTY|Q_ENUM|Q_DECLARE|signals|emit)\b")


# --------------------------------------------------------------------------
# Model
# --------------------------------------------------------------------------

@dataclass
class Param:
    type: str          # exactly as written, e.g. "const QString &"
    name: str
    default: str = ""

    @property
    def is_pointer(self) -> bool:
        return self.type.rstrip().endswith("*")

    @property
    def is_const(self) -> bool:
        return self.type.strip().startswith("const")

    @property
    def value_type(self) -> str:
        """The type to store in a data column: no const, no reference."""
        text = self.type.strip()
        if text.startswith("const "):
            text = text[len("const "):]
        text = text.rstrip("& ").strip()
        return re.sub(r"\s+", " ", text)


@dataclass
class Function:
    name: str
    return_type: str
    params: list[Param] = field(default_factory=list)
    namespace: str = ""
    record: str = ""       # enclosing struct/class, empty for free functions
    is_static: bool = False
    is_const: bool = False

    @property
    def scope(self) -> str:
        """Namespace prefix, e.g. "jira::"."""
        return f"{self.namespace}::" if self.namespace else ""

    @property
    def owner(self) -> str:
        """Qualified name of the enclosing struct, empty for free functions."""
        return f"{self.scope}{self.record}" if self.record else ""

    @property
    def display(self) -> str:
        qualifier = f"{self.owner}::" if self.record else self.scope
        args = ", ".join(f"{p.type} {p.name}".strip() for p in self.params)
        prefix = "static " if self.is_static else ""
        suffix = " const" if self.is_const else ""
        return f"{prefix}{self.return_type} {qualifier}{self.name}({args}){suffix}"


@dataclass
class Record:
    name: str
    namespace: str = ""
    fields: list[Param] = field(default_factory=list)
    is_enum: bool = False
    constructors: list[list[Param]] = field(default_factory=list)

    @property
    def qualified(self) -> str:
        return f"{self.namespace}::{self.name}" if self.namespace else self.name


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------

def strip_comments(text: str) -> str:
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
        elif c in "\"'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                out.append(text[i])
                i += 1
                if text[i - 1] == quote:
                    break
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_preprocessor(text: str) -> str:
    """Drop #include, #ifdef and friends -- including continued lines."""
    kept: list[str] = []
    continuing = False
    for line in text.splitlines():
        stripped = line.strip()
        if continuing:
            continuing = stripped.endswith("\\")
            continue
        if stripped.startswith("#"):
            continuing = stripped.endswith("\\")
            continue
        kept.append(line)
    return "\n".join(kept)


def split_top_level(text: str, separator: str = ",") -> list[str]:
    """Split on `separator`, ignoring anything inside (), <> or []."""
    parts: list[str] = []
    depth = 0
    current: list[str] = []
    for ch in text:
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth -= 1
        if ch == separator and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    tail = "".join(current).strip()
    if tail:
        parts.append(tail)
    return parts


def parse_param(text: str, index: int) -> Param:
    text = re.sub(r"\s+", " ", text).strip()
    default = ""
    if "=" in text:
        head, _, default = text.partition("=")
        text, default = head.strip(), default.strip()

    match = re.match(r"^(?P<type>.*?)\s*(?P<stars>[*&]*)\s*(?P<name>[A-Za-z_]\w*)$", text)
    if match and match.group("type").strip() and match.group("name") not in TYPE_KEYWORDS:
        type_text = (match.group("type") + " " + match.group("stars")).strip()
        name = match.group("name")
    else:
        # Unnamed parameter ("int", "const QString &"): invent something to
        # QFETCH into, since a column needs a name.
        type_text = text
        name = f"arg{index + 1}"
    return Param(type=re.sub(r"\s+", " ", type_text), name=name, default=default)


def parse_declaration(statement: str, namespace: str = "", record: str = "") -> Function | None:
    """Turn one normalised declaration into a Function, or None if it isn't one."""
    statement = re.sub(r"\s+", " ", statement).strip().rstrip(";").strip()
    if not statement or "(" not in statement or DECL_SKIP.search(statement):
        return None

    open_paren = statement.find("(")
    if "=" in statement[:open_paren]:
        return None
    depth, close_paren = 0, -1
    for i in range(open_paren, len(statement)):
        if statement[i] == "(":
            depth += 1
        elif statement[i] == ")":
            depth -= 1
            if depth == 0:
                close_paren = i
                break
    if close_paren < 0:
        return None

    head = statement[:open_paren].strip()
    params_text = statement[open_paren + 1:close_paren].strip()
    tail = statement[close_paren + 1:].strip()

    if "=" in tail and re.search(r"=\s*(delete|default)", tail):
        return None
    if any(word in tail for word in ("{", ":")) and not tail.startswith("const"):
        # A constructor initialiser list, not a trailing qualifier.
        tail = tail.split(":", 1)[0]

    tokens = head.split()
    is_static = "static" in tokens
    tokens = [t for t in tokens if t not in {"static", "inline", "virtual", "explicit", "constexpr", "friend", "extern"}]
    if not tokens:
        return None

    name = tokens[-1]
    return_type = " ".join(tokens[:-1]).strip()
    if "::" in name:                      # out-of-line definition: Foo::bar
        owner, _, name = name.rpartition("::")
        record = record or owner.split("::")[-1]
    if not re.fullmatch(r"[A-Za-z_]\w*", name):
        return None
    if not return_type:                   # constructor or destructor
        return None
    if return_type.endswith("&") or return_type.endswith("*"):
        return_type = re.sub(r"\s*([*&])", r" \1", return_type).strip()

    params = []
    if params_text and params_text != "void":
        params = [parse_param(text, i) for i, text in enumerate(split_top_level(params_text))]

    return Function(
        name=name,
        return_type=return_type,
        params=params,
        namespace=namespace,
        record=record,
        is_static=is_static,
        is_const=bool(re.search(r"\bconst\b", tail)),
    )


def parse_signature(text: str) -> Function:
    """Parse a signature typed on the command line, e.g. "jira::User::fromJson(...)"."""
    function = parse_declaration(text)
    if function is None:
        raise ValueError(f"could not read a function declaration from: {text.strip()!r}")
    return function


def _skip_block(text: str, start: int) -> int:
    """`start` indexes a '{'; return the index just past its matching '}'."""
    depth = 0
    i, n = start, len(text)
    while i < n:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        elif text[i] in "\"'":
            quote = text[i]
            i += 1
            while i < n and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
        i += 1
    return n


def parse_header(text: str) -> tuple[list[Function], dict[str, Record]]:
    """Walk a header, collecting function declarations and struct fields.

    The fields matter as much as the functions: when a function returns a struct
    declared in the same header, the generator can turn its members into one
    expectation column each instead of demanding an operator== nobody wrote.
    """
    text = strip_preprocessor(strip_comments(text))
    functions: list[Function] = []
    records: dict[str, Record] = {}
    stack: list[list[str]] = []             # [kind, name, access]
    buffer: list[str] = []
    i, n = 0, len(text)

    def context() -> tuple[str, str, bool]:
        namespace = "::".join(entry[1] for entry in stack if entry[0] == "namespace")
        record, public = "", True
        for entry in reversed(stack):
            if entry[0] in ("struct", "class"):
                record, public = entry[1], entry[2] == "public"
                break
        return namespace, record, public

    while i < n:
        ch = text[i]
        if ch == "{":
            statement = re.sub(r"\s+", " ", "".join(buffer)).strip()
            buffer = []
            namespace, record, public = context()
            keyword = re.match(r"^(namespace|struct|class|enum|union)\b\s*(?:class\s+)?(\w*)", statement)
            if keyword and keyword.group(1) == "namespace":
                stack.append(["namespace", keyword.group(2), "public"])
                i += 1
            elif keyword and keyword.group(1) in ("struct", "class"):
                name = keyword.group(2)
                # A struct's members start public, a class's start private.
                stack.append([keyword.group(1), name, "public" if keyword.group(1) == "struct" else "private"])
                qualified = f"{namespace}::{name}" if namespace else name
                records.setdefault(qualified, Record(name=name, namespace=namespace))
                i += 1
            elif keyword and keyword.group(1) == "enum":
                # The values are of no use here, but the name is: a test outside
                # the namespace has to spell the enum jira::AuthMode.
                name = keyword.group(2)
                if name:
                    qualified = f"{namespace}::{name}" if namespace else name
                    records.setdefault(qualified, Record(name=name, namespace=namespace, is_enum=True))
                i = _skip_block(text, i)
            elif keyword:                     # a union: nothing to collect
                i = _skip_block(text, i)
            else:
                # An inline definition: the declaration is still worth having.
                function = parse_declaration(statement, namespace, record) if public else None
                if function:
                    functions.append(function)
                elif public and record:
                    _remember_constructor(records, statement, namespace, record)
                i = _skip_block(text, i)
        elif ch == "}":
            if stack:
                stack.pop()
            buffer = []
            i += 1
        elif ch == ";":
            statement = re.sub(r"\s+", " ", "".join(buffer)).strip()
            buffer = []
            namespace, record, public = context()
            function = parse_declaration(statement, namespace, record) if public else None
            if function:
                functions.append(function)
            elif record and statement:
                if public and _remember_constructor(records, statement, namespace, record):
                    pass
                elif "(" not in statement.split("=", 1)[0]:
                    member = parse_member(statement.split("=", 1)[0])
                    if member:
                        qualified = f"{namespace}::{record}" if namespace else record
                        records.setdefault(qualified, Record(name=record, namespace=namespace)).fields.append(member)
            i += 1
        elif ch == ":" and ACCESS_PATTERN.search("".join(buffer).strip()):
            label = ACCESS_PATTERN.search("".join(buffer).strip()).group("label")
            if stack and stack[-1][0] in ("struct", "class"):
                # Signals are emitted, not called: nothing here can test one.
                stack[-1][2] = "private" if label in ("signals", "Q_SIGNALS") else label
            buffer = []
            i += 1
        else:
            buffer.append(ch)
            i += 1

    return functions, records


INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
METATYPE_PATTERN = re.compile(r"Q_DECLARE_METATYPE\s*\(\s*([^)]+?)\s*\)")


def load_header(path: Path, depth: int = 2,
                seen: set[Path] | None = None) -> tuple[list[Function], dict[str, Record], set[str]]:
    """Parse `path`, and follow its quoted includes for the structs they declare.

    Only the type names travel: a signature in timesheetexport.h mentions
    TimesheetEntry, which lives in timesheet.h, and a test outside the namespace
    has to spell it jira::TimesheetEntry or it will not compile.
    """
    seen = seen if seen is not None else set()
    resolved = path.resolve()
    if resolved in seen:
        return [], {}, set()
    seen.add(resolved)

    text = path.read_text(encoding="utf-8")
    functions, records = parse_header(text)
    declared = set(METATYPE_PATTERN.findall(text))
    if depth <= 0:
        return functions, records, declared

    roots = [path.parent]
    for parent in path.resolve().parents:
        if parent.name == "src":
            roots.append(parent)
            break
    for include in INCLUDE_PATTERN.findall(text):
        for root in roots:
            candidate = root / include
            if candidate.is_file():
                _, nested, nested_declared = load_header(candidate, depth - 1, seen)
                for key, record in nested.items():
                    records.setdefault(key, record)
                declared |= nested_declared
                break
    return functions, records, declared


CONSTRUCTOR_PREFIXES = ("explicit", "inline", "constexpr")


def parse_constructor(statement: str, record: str) -> list[Param] | None:
    """Parameters of `statement` if it declares a constructor of `record`."""
    statement = re.sub(r"\s+", " ", statement).strip()
    tokens = statement.split()
    while tokens and tokens[0] in CONSTRUCTOR_PREFIXES:
        tokens = tokens[1:]
    statement = " ".join(tokens)
    if not statement.startswith(record) or not statement[len(record):].lstrip().startswith("("):
        return None
    inner = statement[statement.find("(") + 1:statement.rfind(")")]
    if not inner.strip() or inner.strip() == "void":
        return []
    return [parse_param(text, i) for i, text in enumerate(split_top_level(inner))]


def _remember_constructor(records: dict[str, Record], statement: str, namespace: str, record: str) -> bool:
    params = parse_constructor(statement, record)
    if params is None:
        return False
    qualified = f"{namespace}::{record}" if namespace else record
    records.setdefault(qualified, Record(name=record, namespace=namespace)).constructors.append(params)
    return True


def parse_member(statement: str) -> Param | None:
    """A struct data member: "QString displayName", "bool active = true"."""
    statement = statement.strip()
    if not statement or statement.split()[0] in {"using", "typedef", "friend", "return"}:
        return None
    member = parse_param(statement, 0)
    if not re.fullmatch(r"[A-Za-z_]\w*", member.name) or not member.type:
        return None
    if member.name.startswith("arg"):        # no name found: not a member
        return None
    return member


# --------------------------------------------------------------------------
# Generation
# --------------------------------------------------------------------------

def slot_name(function_name: str) -> str:
    """formatDuration -> formatsDuration, matching how the suite names slots."""
    match = re.match(r"^([a-z]+)(.*)$", function_name)
    if not match:
        return function_name
    verb, rest = match.groups()
    if verb in NON_VERB_PREFIXES or verb.endswith("s"):
        return function_name
    if verb.endswith(("sh", "ch", "x", "z")):
        verb += "es"
    elif verb.endswith("y") and len(verb) > 1 and verb[-2] not in "aeiou":
        verb = verb[:-1] + "ies"
    else:
        verb += "s"
    return verb + rest


def default_slot_name(function: Function) -> str:
    """A slot name that is unique across a struct's members.

    Five structs declare fromJson(); the suite is one class, so the struct has
    to be part of the name or the second one fails to compile.
    """
    base = slot_name(function.name)
    if function.record:
        return base + function.record[0].upper() + function.record[1:]
    return base


def template_argument(type_text: str) -> str:
    match = re.match(r"^\w+\s*<\s*(.+?)\s*>$", type_text.strip())
    return match.group(1).strip() if match else ""


def placeholder(type_text: str) -> str:
    """A compiling, obviously-wrong value for a data column."""
    type_text = type_text.strip()
    if type_text == "bool":
        return "false"
    if type_text == "int":
        return "0"
    if type_text == "double":
        return "0.0"
    if type_text in INTEGER_TYPES or type_text in FLOAT_TYPES:
        # A bare literal is an int or a double, and "unsigned int(0)" is not
        # even valid C++. Cast, and the column type is what lands in the row.
        return f"static_cast<{type_text}>(0)"
    if type_text == "QString":
        return "QString()"
    if type_text == "QStringList":
        return "QStringList()"
    if type_text.endswith("*"):
        return "nullptr"
    return f"{type_text}()"


def is_container(type_text: str) -> bool:
    return bool(re.match(r"^(QList|QVector|QVariantList|QStringList|std::vector)\b", type_text.strip()))


class Generator:
    """Builds the slot pair for one function."""

    def __init__(self, function: Function, records: dict[str, Record] | None = None,
                 test_class: str = "TestGenerated", rows: int = 3, max_fields: int = 6,
                 name: str = "", fail_marker: bool = True, declared_metatypes: set[str] | None = None):
        self.function = function
        self.records = records or {}
        self.declared_metatypes = declared_metatypes or set()
        self.test_class = test_class
        self.rows = max(1, rows)
        self.max_fields = max_fields
        self.slot = name or default_slot_name(function)
        self.fail_marker = fail_marker
        self.metatypes: list[str] = []
        self.skipped_fields: list[str] = []

        self.columns: list[Param] = []      # what becomes a data column
        self.locals: list[Param] = []       # pointer arguments, built in the body
        for param in function.params:
            (self.locals if param.is_pointer else self.columns).append(param)

        self.pointer_locals: dict[str, bool] = {}
        for param in self.locals:
            base = param.value_type.rstrip("*").strip()
            record = self._record_for(base)
            constructible = base in QT_VALUE_TYPES or bool(record and not record.is_enum)
            self.pointer_locals[param.name] = constructible and not param.is_const

        self.expectations = self._expectations()
        self.data_driven = bool(self.columns) or len(self.expectations) > 1

    # -- expectations ------------------------------------------------------

    def _qualify(self, type_text: str) -> str:
        """Name a type the way a test outside the namespace has to spell it."""
        scope = self.function.namespace
        if not scope:
            return type_text

        def fix(name: str) -> str:
            if f"{scope}::{name}" in self.records and not name.startswith(f"{scope}::"):
                return f"{scope}::{name}"
            return name

        inner = template_argument(type_text)
        if inner:
            outer = type_text.split("<", 1)[0].strip()
            return f"{outer}<{fix(inner)}>"
        return fix(type_text)

    def _record_for(self, type_text: str) -> Record | None:
        bare = type_text.strip().removeprefix(f"{self.function.namespace}::")
        key = f"{self.function.namespace}::{bare}" if self.function.namespace else bare
        return self.records.get(key)

    def _expectations(self) -> list[tuple[Param, str]]:
        """(column, expression to compare it against) for the return value."""
        returned = self.function.return_type.strip()
        if returned == "void":
            return []
        if is_container(returned):
            # A list of structs: assert the count, and leave the elements to a
            # human who knows which one matters.
            return [(Param(type="int", name="expectedCount"), "actual.size()")]
        record = self._record_for(returned)
        if record and record.fields:
            pairs = []
            for member in record.fields:
                nested = self._record_for(member.value_type)
                if (nested and not nested.is_enum) or is_container(member.value_type):
                    # A nested struct has no operator==, and a list of them has
                    # none either; QCOMPARE would not compile. Named in a TODO
                    # instead -- see Generator.test_function.
                    self.skipped_fields.append(member.name)
                    continue
                column = Param(type=member.value_type, name="expected" + member.name[0].upper() + member.name[1:])
                pairs.append((column, f"actual.{member.name}"))
                if len(pairs) == self.max_fields:
                    break
            if pairs:
                return pairs
        return [(Param(type=returned, name="expected"), "actual")]

    # -- pieces ------------------------------------------------------------

    def _column_type(self, param: Param) -> str:
        qualified = self._qualify(param.value_type)
        if qualified not in KNOWN_METATYPES and not self._is_registered(qualified):
            if qualified not in self.metatypes:
                self.metatypes.append(qualified)
        return qualified

    def _is_registered(self, type_text: str) -> bool:
        if type_text in self.declared_metatypes:
            return True                       # the header said so already
        inner = template_argument(type_text)
        if inner:
            return False                      # QList<Custom> still needs declaring
        return type_text in KNOWN_METATYPES

    def _call(self) -> str:
        arguments = []
        for param in self.function.params:
            if param.is_pointer and self.pointer_locals.get(param.name):
                arguments.append(f"&{param.name}")
            else:
                arguments.append(param.name)
        argument_text = ", ".join(arguments)
        if self.function.record and self.function.is_static:
            return f"{self.function.owner}::{self.function.name}({argument_text})"
        if self.function.record:
            return f"subject.{self.function.name}({argument_text})"
        return f"{self.function.scope}{self.function.name}({argument_text})"

    def _fixture(self) -> list[str]:
        """Declare the object whose method is under test."""
        function = self.function
        record = self._record_for(function.record)
        constructors = record.constructors if record else []

        # A constructor whose arguments all have defaults is as good as none.
        required: list[Param] = []
        if constructors:
            options = [[p for p in params if not p.default] for params in constructors]
            required = min(options, key=len)

        lines: list[str] = []
        taken = {p.name for p in self.function.params}
        arguments: list[str] = []
        for param in required:
            name = param.name if param.name not in taken else f"{param.name}Fixture"
            arguments.append(name)
            if param.is_pointer:
                base = self._qualify(param.value_type.rstrip("*").strip())
                lines.append(f"    {base} *{name} = nullptr;   // TODO: the real collaborator")
            else:
                lines.append(f"    {self._qualify(param.value_type)} {name}{{}};   // TODO: a real value")

        construction = f"({', '.join(arguments)})" if arguments else ""
        lines.append(f"    {function.owner} subject{construction};")
        lines.append("    // TODO: put the fixture into the state under test.")
        return lines

    def declarations(self) -> list[str]:
        lines = []
        if self.data_driven:
            lines.append(f"    void {self.slot}_data();")
        lines.append(f"    void {self.slot}();")
        return lines

    def data_function(self) -> list[str]:
        if not self.data_driven:
            return []
        lines = [f"void {self.test_class}::{self.slot}_data()", "{"]
        for param in self.columns:
            lines.append(f'    QTest::addColumn<{self._column_type(param)}>("{param.name}");')
        for column, _ in self.expectations:
            lines.append(f'    QTest::addColumn<{self._column_type(column)}>("{column.name}");')
        lines.append("")
        lines.append("    // TODO: replace these with cases worth having. They are named")
        lines.append(f'    //       "{PLACEHOLDER_TAG}" so the test itself can tell it is unfinished;')
        lines.append("    //       rename them and the guard above stops firing.")
        for index in range(self.rows):
            values = [placeholder(self._column_type(param)) for param in self.columns]
            values += [placeholder(self._column_type(column)) for column, _ in self.expectations]
            row = " << ".join(values)
            separator = " << " if row else ""
            lines.append(f'    QTest::newRow("{PLACEHOLDER_TAG} {index + 1}"){separator}{row};')
        lines.append("}")
        return lines

    def test_function(self) -> list[str]:
        function = self.function
        lines = [f"void {self.test_class}::{self.slot}()", "{"]

        if self.data_driven:
            if self.fail_marker:
                # Self-disabling: rename the rows and this stops firing.
                lines.append(f'    QVERIFY2(qstrncmp(QTest::currentDataTag(), "{PLACEHOLDER_TAG}", '
                             f'{len(PLACEHOLDER_TAG)}) != 0,')
                lines.append('             "qtestgen skeleton: replace the placeholder rows with real cases");')
                lines.append("")
            for param in self.columns:
                lines.append(f"    QFETCH({self._column_type(param)}, {param.name});")
            for column, _ in self.expectations:
                lines.append(f"    QFETCH({self._column_type(column)}, {column.name});")
            lines.append("")

        if function.record and not function.is_static:
            lines.extend(self._fixture())
            lines.append("")

        for param in self.locals:
            base = self._qualify(param.value_type.rstrip("*").strip())
            if self.pointer_locals.get(param.name):
                lines.append(f"    {base} {param.name}{{}};   // filled in by the call")
            else:
                lines.append(f"    {base} *{param.name} = nullptr;   // TODO: point this at a fixture")
        if self.locals:
            lines.append("")

        call = self._call()
        returned = function.return_type.strip()
        if returned == "void":
            lines.append(f"    {call};")
            lines.append("")
            lines.append("    // TODO: assert the observable effect -- an out-parameter above, a")
            lines.append("    //       getter, a signal caught with QSignalSpy.")
            if self.fail_marker:
                lines.append('    QFAIL("qtestgen skeleton: assert something, then delete this line");')
        else:
            lines.append(f"    const {self._qualify(returned)} actual = {call};")
            lines.append("")
            if not self.data_driven:
                lines.append("    // TODO: what should the fixture above produce?")
            for column, expression in self.expectations:
                wanted = column.name if self.data_driven else placeholder(self._column_type(column))
                lines.append(f"    QCOMPARE({expression}, {wanted});")
            if not self.data_driven and self.fail_marker:
                lines.append('    QFAIL("qtestgen skeleton: set the expected value above, then delete this line");')
            if is_container(returned):
                lines.append("    // TODO: the count alone proves little -- assert on the elements too.")
            if self.skipped_fields:
                skipped = ", ".join(self.skipped_fields)
                lines.append(f"    // Left out, having no operator== to compare with: {skipped}.")
                lines.append("    // Reach into their own members instead.")
        lines.append("}")
        return lines

    def slots(self) -> list[str]:
        body = self.data_function()
        if body:
            body.append("")
        return body + self.test_function()


# --------------------------------------------------------------------------
# Assembly
# --------------------------------------------------------------------------

def header_include(header: Path | None) -> str:
    if header is None:
        return ""
    parts = header.resolve().parts
    if "src" in parts:                        # mirror how the suite includes core
        return "/".join(parts[parts.index("src") + 1:])
    return header.name


def standalone_file(generators: list[Generator], test_class: str, include: str, moc_name: str) -> str:
    metatypes: list[str] = []
    for generator in generators:
        generator.slots()                     # populates generator.metatypes
        for name in generator.metatypes:
            if name not in metatypes:
                metatypes.append(name)

    lines: list[str] = []
    if include:
        lines.append(f'#include "{include}"')
        lines.append("")
    lines.append("#include <QTest>")
    lines.append("")
    if metatypes:
        lines.extend(metatype_block(metatypes))
        lines.append("")
    lines.append(f"class {test_class} : public QObject")
    lines.append("{")
    lines.append("    Q_OBJECT")
    lines.append("")
    lines.append("private slots:")
    for generator in generators:
        lines.extend(generator.declarations())
    lines.append("};")
    lines.append("")
    for generator in generators:
        lines.extend(generator.slots())
        lines.append("")
    lines.append(f"QTEST_GUILESS_MAIN({test_class})")
    lines.append(f'#include "{moc_name}.moc"')
    return "\n".join(lines) + "\n"


def metatype_block(names: list[str]) -> list[str]:
    """The Q_DECLARE_METATYPE lines a Qt 5 build needs for these column types.

    Qt 6 registers any complete type the first time a column asks for it, so the
    block is guarded: outside the guard it would redefine whatever Qt 6 already
    knows. If a Qt 5 build still reports a redefinition, Qt declares that type
    itself -- delete the line.
    """
    lines = [
        "// QTest::addColumn needs a registered metatype. Qt 6 registers any",
        "// complete type on first use; Qt 5.15 does not, so this is for Qt 5.",
        "#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)",
    ]
    lines.extend(f"Q_DECLARE_METATYPE({name})" for name in names)
    lines.append("#endif")
    return lines


def cmake_snippet(target: str) -> str:
    return "\n".join([
        "# Add to tests/CMakeLists.txt:",
        f"if(QT_VERSION_MAJOR EQUAL 6)",
        f"    qt_add_executable({target} {target}.cpp)",
        f"else()",
        f"    add_executable({target} {target}.cpp)",
        f"endif()",
        f"target_link_libraries({target} PRIVATE jiradesk_core Qt${{QT_VERSION_MAJOR}}::Test)",
        f"add_test(NAME {target.removeprefix('tst_')} COMMAND {target})",
    ])


def insert_into(path: Path, generators: list[Generator], force: bool = False) -> str:
    """Splice slots into an existing QTest file, where its own slots live."""
    text = path.read_text(encoding="utf-8")
    test_class = re.search(r"class\s+(\w+)\s*:\s*public\s+QObject", text)
    if not test_class:
        raise ValueError("%s: no `class X : public QObject` to add slots to" % path)
    class_name = test_class.group(1)

    declarations: list[str] = []
    definitions: list[str] = []
    metatypes: list[str] = []
    for generator in generators:
        generator.test_class = class_name
        if not force and re.search(rf"\b{re.escape(generator.slot)}\s*\(", text):
            raise ValueError(f"{path}: {generator.slot}() is already there (use --force to add it anyway)")
        body = generator.slots()
        declarations.extend(generator.declarations())
        definitions.extend(body + [""])
        for name in generator.metatypes:
            if name not in metatypes and f"Q_DECLARE_METATYPE({name})" not in text:
                metatypes.append(name)

    slots_marker = text.find("private slots:")
    if slots_marker < 0:
        raise ValueError("%s: no `private slots:` block found" % path)
    class_end = text.find("};", slots_marker)
    if class_end < 0:
        raise ValueError("%s: the test class has no closing `};`" % path)
    text = text[:class_end] + "\n".join(declarations) + "\n" + text[class_end:]

    tail = re.search(r"^QTEST_\w+\(", text, re.MULTILINE)
    insert_at = tail.start() if tail else len(text)
    # A trailing "" in the list is one newline, not a blank line: the block has
    # to end with two, or the next definition butts up against this one.
    block = "\n".join(definitions).rstrip("\n") + "\n\n"
    text = text[:insert_at] + block + text[insert_at:]

    if metatypes:
        anchor = text.find(f"class {class_name}")
        block = "\n".join(metatype_block(metatypes)) + "\n\n"
        text = text[:anchor] + block + text[anchor:]

    path.write_text(text, encoding="utf-8")
    return class_name


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="qtestgen",
        description="Generate Qt Test skeletons from C++ function declarations.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=EXAMPLES,
    )
    parser.add_argument("signature", nargs="?", help='a declaration, e.g. "QString formatDuration(int seconds)"')
    parser.add_argument("--header", type=Path, help="read the declaration from this header")
    parser.add_argument("--function", action="append", default=[], help="name to take from the header (repeatable)")
    parser.add_argument("--all", action="store_true", help="every function the header declares")
    parser.add_argument("--list", action="store_true", help="list what the header declares, and stop")
    parser.add_argument("--name", help="name the test slot yourself")
    parser.add_argument("--class", dest="test_class", default="TestGenerated", help="test class name")
    parser.add_argument("--rows", type=int, default=3, help="placeholder data rows (default 3)")
    parser.add_argument("--max-fields", type=int, default=6, help="expectation columns for a struct return (default 6)")
    parser.add_argument("--standalone", action="store_true", help="a whole tst_*.cpp, plus the CMake lines")
    parser.add_argument("--insert", type=Path, help="splice the slots into this existing test file")
    parser.add_argument("--force", action="store_true", help="insert even if a slot of that name exists")
    parser.add_argument("--no-fail-marker", action="store_true", help="omit the QFAIL in bodies with nothing to assert")
    parser.add_argument("-o", "--output", type=Path, help="write to this file instead of stdout")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    records: dict[str, Record] = {}
    declared: list[Function] = []
    declared_metatypes: set[str] = set()
    if args.header:
        if not args.header.is_file():
            print(f"qtestgen: no such header: {args.header}", file=sys.stderr)
            return 2
        declared, records, declared_metatypes = load_header(args.header)

    if args.list:
        if not declared:
            print("qtestgen: --list needs --header", file=sys.stderr)
            return 2
        for function in declared:
            print(function.display)
        return 0

    selected: list[Function] = []
    if args.signature:
        try:
            selected.append(parse_signature(args.signature))
        except ValueError as error:
            print(f"qtestgen: {error}", file=sys.stderr)
            return 2
    if args.all:
        selected.extend(declared)
    for wanted in args.function:
        record, _, bare = wanted.rpartition("::")
        matches = [f for f in declared if f.name == bare and (not record or f.record == record)]
        if not matches:
            print(f"qtestgen: {args.header} does not declare {wanted}()", file=sys.stderr)
            return 2
        selected.extend(matches)

    if not selected:
        print("qtestgen: nothing to generate -- pass a signature, --function or --all", file=sys.stderr)
        return 2
    if args.name and len(selected) > 1:
        print("qtestgen: --name only makes sense for a single function", file=sys.stderr)
        return 2

    test_class = args.test_class
    generators = [
        Generator(function, records, test_class=test_class, rows=args.rows,
                  max_fields=args.max_fields, name=args.name or "",
                  fail_marker=not args.no_fail_marker, declared_metatypes=declared_metatypes)
        for function in selected
    ]

    if args.insert:
        try:
            class_name = insert_into(args.insert, generators, force=args.force)
        except (ValueError, OSError) as error:
            print(f"qtestgen: {error}", file=sys.stderr)
            return 1
        names = ", ".join(f"{g.slot}()" for g in generators)
        print(f"qtestgen: added {names} to {class_name} in {args.insert}", file=sys.stderr)
        return 0

    if args.standalone:
        target = args.output.stem if args.output else f"tst_{selected[0].name.lower()}"
        output = standalone_file(generators, test_class, header_include(args.header), target)
        note = cmake_snippet(target)
    else:
        chunks = []
        for generator in generators:
            body = "\n".join(generator.slots())
            declaration = "\n".join(generator.declarations())
            chunks.append(f"// Declare in the class:\n{declaration}\n\n{body}")
        output = "\n\n".join(chunks) + "\n"
        metatypes = [name for generator in generators for name in generator.metatypes]
        note = ""
        if metatypes:
            unique = list(dict.fromkeys(metatypes))
            note = "\n".join(["# Also needed, above the test class:"] + metatype_block(unique))

    if args.output:
        args.output.write_text(output, encoding="utf-8")
        print(f"qtestgen: wrote {args.output}", file=sys.stderr)
    else:
        sys.stdout.write(output)
    if note:
        print("\n" + note, file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
