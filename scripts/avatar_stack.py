"""TAB5 compatibility fixes for M5Stack-Avatar v0.10.0."""
from pathlib import Path
import re

Import("env")

source = (Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
          / "M5Stack-Avatar" / "src" / "Avatar.cpp")
text = source.read_text()
pattern = r'("drawLoop",\s*/\* Name of the task \*/\s*)2048(,)'
updated, count = re.subn(pattern, r'\g<1>16384\2', text)
if count:
    source.write_text(updated)
elif not re.search(r'"drawLoop",\s*/\* Name of the task \*/\s*16384,', text):
    raise RuntimeError("Avatar draw task changed; review TAB5 stack adjustment")

# Cross-core suspend/resume during a framebuffer transfer can leave drawing
# stopped on TAB5. Publish expressions atomically without suspending the task.
header = source.with_name("Avatar.h")
header_text = header.read_text()
if "#include <atomic>" not in header_text:
    header_text = header_text.replace('#include <M5GFX.h>', '#include <M5GFX.h>\n#include <atomic>')
header_text = header_text.replace('  Expression expression;', '  std::atomic<Expression> expression;')
if 'std::atomic<Expression> expression;' not in header_text:
    raise RuntimeError("Avatar expression field changed; review TAB5 compatibility")
if header.read_text() != header_text:
    header.write_text(header_text)

text = source.read_text()
original = '''void Avatar::setExpression(Expression expression) {
  suspend();
  this->expression = expression;
  resume();
}'''
replacement = '''void Avatar::setExpression(Expression expression) {
  this->expression.store(expression);
}'''
if original in text:
    source.write_text(text.replace(original, replacement))
elif replacement not in text:
    raise RuntimeError("Avatar expression setter changed; review TAB5 compatibility")
