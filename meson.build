project('6502', 'c', 'cpp', default_options: ['cpp_std=c++20'])

executable('6502', '6502.cpp')

gtest_proj = subproject('gtest')
gtest_dep = gtest_proj.get_variable('gtest_dep')
gtest_main_dep = gtest_proj.get_variable('gtest_main_dep')

test_opcode_exe = executable('test_opcodes', 'test_opcodes.cpp', dependencies: [gtest_dep, gtest_main_dep])
test('test ze opcodes', test_opcode_exe)

test_mem_exe = executable('test_mem', 'test_mem.cpp', dependencies: [gtest_dep, gtest_main_dep])
test('test ze mem', test_mem_exe)
