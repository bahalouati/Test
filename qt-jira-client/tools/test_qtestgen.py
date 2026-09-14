"""Tests for qtestgen.

Run from anywhere:

    python -m unittest discover -s qt-jira-client/tools -t qt-jira-client/tools

The cases below are the ones that actually went wrong while the generator was
being written against this repo's own headers -- a member initialiser read as a
declaration, a bare 0 pushed into a qint64 column, Q_OBJECT swallowing the
"public:" that followed it.
"""

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import qtestgen  # noqa: E402


class ParseSignature(unittest.TestCase):
    def test_reads_a_free_function(self):
        function = qtestgen.parse_signature("QString formatDuration(int seconds)")
        self.assertEqual(function.name, "formatDuration")
        self.assertEqual(function.return_type, "QString")
        self.assertEqual([p.name for p in function.params], ["seconds"])
        self.assertEqual(function.params[0].value_type, "int")

    def test_strips_const_and_reference_for_the_column_type(self):
        function = qtestgen.parse_signature("bool isValidDuration(const QString &text)")
        self.assertEqual(function.params[0].value_type, "QString")
        self.assertFalse(function.params[0].is_pointer)

    def test_keeps_a_default_argument_out_of_the_type(self):
        function = qtestgen.parse_signature("AuthMode authModeFromString(const QString &text, AuthMode fallback = AuthMode::Bearer)")
        self.assertEqual(function.params[1].value_type, "AuthMode")
        self.assertEqual(function.params[1].default, "AuthMode::Bearer")

    def test_splits_on_commas_outside_template_arguments(self):
        function = qtestgen.parse_signature("void f(QHash<QString, int> counts, int n)")
        self.assertEqual([p.name for p in function.params], ["counts", "n"])

    def test_notices_static_and_const(self):
        self.assertTrue(qtestgen.parse_signature("static User fromJson(const QJsonObject &o)").is_static)
        self.assertTrue(qtestgen.parse_signature("bool isComplete() const").is_const)

    def test_names_an_unnamed_parameter(self):
        function = qtestgen.parse_signature("void f(int, const QString &)")
        self.assertEqual([p.name for p in function.params], ["arg1", "arg2"])

    def test_rejects_something_that_is_not_a_declaration(self):
        with self.assertRaises(ValueError):
            qtestgen.parse_signature("not a function at all")


class ParseHeader(unittest.TestCase):
    HEADER = textwrap.dedent("""
        #pragma once
        #include <QString>

        namespace jira {

        enum class AuthMode { Basic, Bearer };

        QString normalizeBaseUrl(const QString &raw);

        struct User {
            QString displayName;
            // An initialiser that happens to be a call is still a member.
            QString marker = QStringLiteral("x");
            qint64 size = 0;
            bool isNull() const { return displayName.isEmpty(); }
            static User fromJson(const QJsonObject &object);
        };

        class Loader : public QObject
        {
            Q_OBJECT

        public:
            explicit Loader(Client *client, QObject *parent = nullptr);
            void load(const QDate &from);

        signals:
            void finished(const QString &message);

        private:
            void pumpQueue();
        };

        } // namespace jira
    """)

    def setUp(self):
        self.functions, self.records = qtestgen.parse_header(self.HEADER)
        self.by_name = {f.name: f for f in self.functions}

    def test_finds_free_and_member_functions(self):
        self.assertIn("normalizeBaseUrl", self.by_name)
        self.assertIn("fromJson", self.by_name)
        self.assertEqual(self.by_name["normalizeBaseUrl"].namespace, "jira")
        self.assertEqual(self.by_name["fromJson"].record, "User")

    def test_keeps_an_inline_body_out_of_the_way(self):
        self.assertIn("isNull", self.by_name)
        self.assertEqual(self.by_name["isNull"].return_type, "bool")

    def test_skips_private_members_and_signals(self):
        # Q_OBJECT carries no semicolon; the "public:" after it must still count.
        self.assertIn("load", self.by_name)
        self.assertNotIn("pumpQueue", self.by_name)
        self.assertNotIn("finished", self.by_name)

    def test_does_not_mistake_a_member_initialiser_for_a_function(self):
        self.assertNotIn("QStringLiteral", self.by_name)

    def test_collects_struct_fields(self):
        fields = {f.name: f.value_type for f in self.records["jira::User"].fields}
        self.assertEqual(fields, {"displayName": "QString", "marker": "QString", "size": "qint64"})

    def test_registers_an_enum_by_name_only(self):
        self.assertTrue(self.records["jira::AuthMode"].is_enum)
        self.assertEqual(self.records["jira::AuthMode"].fields, [])

    def test_remembers_a_constructor(self):
        constructors = self.records["jira::Loader"].constructors
        self.assertEqual(len(constructors), 1)
        self.assertEqual([p.name for p in constructors[0]], ["client", "parent"])

    def test_a_constructor_is_not_a_test_subject(self):
        self.assertNotIn("Loader", self.by_name)


class SlotNames(unittest.TestCase):
    def test_reads_as_a_sentence(self):
        self.assertEqual(qtestgen.slot_name("formatDuration"), "formatsDuration")
        self.assertEqual(qtestgen.slot_name("parseDateTime"), "parsesDateTime")

    def test_leaves_predicates_and_prepositions_alone(self):
        self.assertEqual(qtestgen.slot_name("isValidDuration"), "isValidDuration")
        self.assertEqual(qtestgen.slot_name("hasWorklogs"), "hasWorklogs")
        self.assertEqual(qtestgen.slot_name("fromJson"), "fromJson")

    def test_a_member_slot_carries_its_struct(self):
        # Five structs declare fromJson(); one class cannot hold five slots of
        # the same name.
        user = qtestgen.Function(name="fromJson", return_type="User", record="User")
        comment = qtestgen.Function(name="fromJson", return_type="Comment", record="Comment")
        self.assertEqual(qtestgen.default_slot_name(user), "fromJsonUser")
        self.assertEqual(qtestgen.default_slot_name(comment), "fromJsonComment")


class Placeholders(unittest.TestCase):
    def test_a_column_gets_a_value_of_exactly_its_type(self):
        # QTest aborts the run on a data-type mismatch, so a bare 0 in a qint64
        # column is fatal rather than merely wrong.
        self.assertEqual(qtestgen.placeholder("qint64"), "static_cast<qint64>(0)")
        self.assertEqual(qtestgen.placeholder("unsigned int"), "static_cast<unsigned int>(0)")
        self.assertEqual(qtestgen.placeholder("int"), "0")
        self.assertEqual(qtestgen.placeholder("double"), "0.0")
        self.assertEqual(qtestgen.placeholder("bool"), "false")
        self.assertEqual(qtestgen.placeholder("QString"), "QString()")
        self.assertEqual(qtestgen.placeholder("jira::Error"), "jira::Error()")


class Generate(unittest.TestCase):
    def generate(self, signature, **kwargs):
        function = qtestgen.parse_signature(signature)
        generator = qtestgen.Generator(function, test_class="TestX", **kwargs)
        return "\n".join(generator.slots()), generator

    def test_builds_a_data_driven_pair(self):
        body, generator = self.generate("QString formatDuration(int seconds)")
        self.assertIn('QTest::addColumn<int>("seconds");', body)
        self.assertIn('QTest::addColumn<QString>("expected");', body)
        self.assertIn("QFETCH(int, seconds);", body)
        self.assertIn("const QString actual = formatDuration(seconds);", body)
        self.assertIn("QCOMPARE(actual, expected);", body)
        self.assertEqual(generator.declarations(), ["    void formatsDuration_data();", "    void formatsDuration();"])

    def test_placeholder_rows_cannot_pass_unnoticed(self):
        body, _ = self.generate("QString formatDuration(int seconds)")
        self.assertIn('QTest::newRow("TODO 1")', body)
        self.assertIn('qstrncmp(QTest::currentDataTag(), "TODO", 4) != 0', body)

    def test_the_guard_can_be_turned_off(self):
        body, _ = self.generate("QString formatDuration(int seconds)", fail_marker=False)
        self.assertNotIn("qstrncmp", body)

    def test_a_void_function_says_what_is_missing(self):
        body, _ = self.generate("void cancel()")
        self.assertIn("cancel();", body)
        self.assertIn("QFAIL(", body)
        self.assertNotIn("QCOMPARE", body)

    def test_row_count_is_configurable(self):
        body, _ = self.generate("QString f(int n)", rows=1)
        self.assertEqual(body.count("QTest::newRow"), 1)


class GenerateWithHeader(unittest.TestCase):
    HEADER = textwrap.dedent("""
        namespace jira {
        enum class AuthMode { Basic, Bearer };
        struct User {
            QString displayName;
            bool active = true;
        };
        struct Comment {
            QString body;
            User author;
            QList<User> readers;
            static Comment fromJson(const QJsonObject &object);
            static QList<Comment> listFromJson(const QJsonObject &page);
        };
        struct Rules { int hours = 0; };
        bool exportWorkbook(const QString &path, const Rules &rules, QString *errorMessage = nullptr);
        class Sheet {
        public:
            explicit Sheet(const QString &name);
            int rowCount() const;
        };
        }
    """)

    def setUp(self):
        self.functions, self.records = qtestgen.parse_header(self.HEADER)
        self.by_name = {(f.record, f.name): f for f in self.functions}

    def generate(self, record, name, **kwargs):
        generator = qtestgen.Generator(self.by_name[(record, name)], self.records, test_class="TestX", **kwargs)
        return "\n".join(generator.slots()), generator

    def test_a_returned_struct_becomes_one_column_per_field(self):
        body, _ = self.generate("Comment", "fromJson")
        self.assertIn('QTest::addColumn<QString>("expectedBody");', body)
        self.assertIn("QCOMPARE(actual.body, expectedBody);", body)

    def test_fields_with_no_operator_are_named_rather_than_compared(self):
        body, _ = self.generate("Comment", "fromJson")
        self.assertNotIn("expectedAuthor", body)
        self.assertNotIn("expectedReaders", body)
        self.assertIn("no operator== to compare with: author, readers", body)

    def test_a_returned_list_is_counted(self):
        body, _ = self.generate("Comment", "listFromJson")
        self.assertIn('QTest::addColumn<int>("expectedCount");', body)
        self.assertIn("QCOMPARE(actual.size(), expectedCount);", body)

    def test_types_are_qualified_for_a_test_outside_the_namespace(self):
        body, _ = self.generate("", "exportWorkbook")
        self.assertIn('QTest::addColumn<jira::Rules>("rules");', body)
        self.assertIn("jira::exportWorkbook(", body)

    def test_an_out_parameter_is_built_and_passed_by_address(self):
        body, _ = self.generate("", "exportWorkbook")
        self.assertIn("QString errorMessage{};", body)
        self.assertIn("&errorMessage)", body)
        self.assertNotIn('addColumn<QString *>', body)

    def test_a_fixture_uses_the_declared_constructor(self):
        body, _ = self.generate("Sheet", "rowCount")
        self.assertIn("QString name{};", body)
        self.assertIn("jira::Sheet subject(name);", body)

    def test_only_custom_types_are_declared_as_metatypes(self):
        _, generator = self.generate("", "exportWorkbook")
        self.assertIn("jira::Rules", generator.metatypes)
        self.assertNotIn("QString", generator.metatypes)

    def test_the_metatype_block_is_guarded_for_qt5(self):
        block = "\n".join(qtestgen.metatype_block(["jira::Rules"]))
        self.assertIn("#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)", block)
        self.assertIn("Q_DECLARE_METATYPE(jira::Rules)", block)
        self.assertIn("#endif", block)

    def test_a_metatype_the_header_already_declares_is_left_alone(self):
        _, generator = self.generate("", "exportWorkbook", declared_metatypes={"jira::Rules"})
        self.assertEqual(generator.metatypes, [])


class Insert(unittest.TestCase):
    SUITE = textwrap.dedent("""\
        #include <QTest>

        class TestJiraCore : public QObject
        {
            Q_OBJECT

        private slots:
            void existing();
        };

        void TestJiraCore::existing()
        {
            QVERIFY(true);
        }

        QTEST_GUILESS_MAIN(TestJiraCore)
        #include "tst_jiracore.moc"
    """)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "tst_jiracore.cpp"
        self.path.write_text(self.SUITE, encoding="utf-8")

    def generator(self, signature="QString formatDuration(int seconds)"):
        return qtestgen.Generator(qtestgen.parse_signature(signature))

    def test_adds_declarations_and_definitions_in_the_right_places(self):
        class_name = qtestgen.insert_into(self.path, [self.generator()])
        text = self.path.read_text(encoding="utf-8")
        self.assertEqual(class_name, "TestJiraCore")
        self.assertLess(text.index("void formatsDuration_data();"), text.index("};"))
        self.assertLess(text.index("void TestJiraCore::formatsDuration_data()"), text.index("QTEST_GUILESS_MAIN"))
        self.assertIn("void TestJiraCore::existing()", text)

    def test_leaves_a_blank_line_before_what_follows(self):
        qtestgen.insert_into(self.path, [self.generator()])
        text = self.path.read_text(encoding="utf-8")
        self.assertIn("}\n\nQTEST_GUILESS_MAIN", text)

    def test_refuses_to_add_the_same_slot_twice(self):
        qtestgen.insert_into(self.path, [self.generator()])
        with self.assertRaises(ValueError):
            qtestgen.insert_into(self.path, [self.generator()])

    def test_force_overrides_the_refusal(self):
        qtestgen.insert_into(self.path, [self.generator()])
        qtestgen.insert_into(self.path, [self.generator()], force=True)
        self.assertEqual(self.path.read_text(encoding="utf-8").count("void TestJiraCore::formatsDuration()"), 2)

    def test_a_file_with_no_test_class_is_refused(self):
        stray = Path(self.directory.name) / "stray.cpp"
        stray.write_text("int main() { return 0; }\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            qtestgen.insert_into(stray, [self.generator()])


class CommandLine(unittest.TestCase):
    HEADER = "namespace jira {\nQString issueSortKey(const QString &key);\n}\n"

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.header = Path(self.directory.name) / "jiratypes.h"
        self.header.write_text(self.HEADER, encoding="utf-8")

    def test_writes_a_standalone_file(self):
        output = Path(self.directory.name) / "tst_generated.cpp"
        code = qtestgen.main(["--header", str(self.header), "--all", "--standalone", "-o", str(output)])
        self.assertEqual(code, 0)
        text = output.read_text(encoding="utf-8")
        self.assertIn("#include <QTest>", text)
        self.assertIn("QTEST_GUILESS_MAIN(TestGenerated)", text)
        self.assertIn('#include "tst_generated.moc"', text)

    def test_reports_a_function_that_is_not_there(self):
        self.assertEqual(qtestgen.main(["--header", str(self.header), "--function", "nope"]), 2)

    def test_reports_a_missing_header(self):
        self.assertEqual(qtestgen.main(["--header", str(self.header) + ".missing", "--all"]), 2)

    def test_needs_something_to_generate(self):
        self.assertEqual(qtestgen.main(["--header", str(self.header)]), 2)


if __name__ == "__main__":
    unittest.main()
