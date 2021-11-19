import os
import re


def clean_shm():
    os.system("ipcs > tmp_")

    skip_lines = 2
    match_start = False
    with open("tmp_", "r") as f:
        for line in f:
            match_start = match_start or re.match("[-]*.*Shared Memory Segments.*[-]*",line) != None
            if not match_start:
                continue
            match_end = re.match("[-]*.*Semaphore Arrays.*[-]*",line) != None
            if match_end:
                break

            if skip_lines:
                skip_lines -= 1
                continue

            line = line.strip().split()

            if len(line) < 6:
                continue

            shm_id = int(line[1])
            natt = int(line[5])

            if natt == 0:
                cmd = "ipcrm shm {}".format(shm_id)
                print(cmd)
                ret = os.system(cmd)
                if ret != 0:
                    print("error")
                    exit(1)

    os.system("rm tmp_")

def clean_sem():
    os.system("ipcs > tmp_")

    skip_lines = 2
    match_start = False
    with open("tmp_", "r") as f:
        for line in f:
            match_start = match_start or re.match("[-]*.*Semaphore Arrays.*[-]*",line) != None
            if not match_start:
                continue

            if skip_lines:
                skip_lines -= 1
                continue

            line = line.strip().split()

            if len(line) < 2:
                continue

            sem_id = int(line[1])
            cmd = "ipcrm sem {}".format(sem_id)
            print(cmd)
            ret = os.system(cmd)
            if ret != 0:
                print("error")
                exit(1)

    os.system("rm tmp_")

clean_shm()
clean_sem()
