#!/usr/bin/python3

import sys
import os, glob, shutil

if len(sys.argv) != 3:
    print("Usage: %s <cov dir> <output dir>" % sys.argv[0])
    sys.exit(1)

target_dir = sys.argv[1]
output_dir = sys.argv[2]

shutil.rmtree(output_dir, ignore_errors=True)
os.makedirs(output_dir, exist_ok=True)

res_copy_dir = os.path.join(output_dir, "res_copy")
try:
    os.mkdir(res_copy_dir)
except:
    pass

summary_fn = os.path.join(output_dir, "summary.csv")
summary_f = open(summary_fn, "w")
summary_f.write("fn,func_cov,num_func,%,bb_cov,num_bb,%\n")

cov_files = glob.glob("{}/**/*.cov".format(target_dir), recursive=True)
num_files = len(cov_files)

total_num_func = 0
total_num_func_covered = 0
total_num_bb = 0
total_num_bb_covered = 0

for fn in cov_files:
    filename = fn.split("/")[-1]
    res_copy_fn = os.path.join(res_copy_dir, filename)
    os.system("cp {} {}".format(fn, res_copy_fn))

    with open(fn) as f1:
        cur_func = ""
        file_cov = dict()
        try:
            for line in f1:
                line = line.strip()
                cov_type = line[0]
                entity_name = line[2:-2]
                is_covered = line[-1] == "1"

                if cov_type == "F":
                    cur_func = entity_name
                    file_cov[cur_func] = dict()
                else:
                    file_cov[cur_func][entity_name] = is_covered
        except:
            print("Error in {}".format(fn))
            continue

    num_func = len(file_cov)
    num_func_covered = 0

    num_bb = 0
    num_bb_covered = 0

    for func in file_cov:
        num_bb += len(file_cov[func])

        func_covered = False
        for bb in file_cov[func]:
            if file_cov[func][bb]:
                num_bb_covered += 1
                func_covered = True

        if func_covered:
            num_func_covered += 1

    if num_func == 0:
        func_cov = 0
    else:
        func_cov = num_func_covered / num_func * 100

    if num_bb == 0:
        bb_cov = 0
    else:
        bb_cov = num_bb_covered / num_bb * 100

    print(
        "{} : {}/{} ({:.2f}%) func cov, {}/{} ({:.2f}%) bb cov".format(
            fn, num_func_covered, num_func, func_cov, num_bb_covered, num_bb, bb_cov
        )
    )
    summary_f.write(
        "{},{},{},{:.2f}%,{},{},{:.2f}%\n".format(
            fn, num_func_covered, num_func, func_cov, num_bb_covered, num_bb, bb_cov
        )
    )

    total_num_func += num_func
    total_num_func_covered += num_func_covered
    total_num_bb += num_bb
    total_num_bb_covered += num_bb_covered

if total_num_func == 0:
    total_func_cov = 0
else:
    total_func_cov = total_num_func_covered / total_num_func * 100

if total_num_bb == 0:
    total_bb_cov = 0
else:
    total_bb_cov = total_num_bb_covered / total_num_bb * 100

print(
    "Total: {} files, {}/{} ({:.2f}%) func cov, {}/{} ({:.2f}%) bb cov".format(
        num_files,
        total_num_func_covered,
        total_num_func,
        total_func_cov,
        total_num_bb_covered,
        total_num_bb,
        total_bb_cov,
    )
)

summary_f.write(
    "Total,{},{},{:.2f}%,{},{},{:.2f}%\n".format(
        total_num_func_covered,
        total_num_func,
        total_func_cov,
        total_num_bb_covered,
        total_num_bb,
        total_bb_cov,
    )
)
