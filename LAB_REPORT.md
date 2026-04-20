# PES-VCS Lab Report

---

## Screenshots

### Phase 1: Object Storage

**Screenshot 1A** — `./test_objects` output showing all tests passing:

![Screenshot 1A](1.a.jpeg)

**Screenshot 1B** — `find .pes/objects -type f` showing sharded directory structure:

![Screenshot 1B](1.b.jpeg)

### Phase 2: Tree Objects

**Screenshot 2A** — `./test_tree` output showing all tests passing:

![Screenshot 2A](2.a.jpeg)

**Screenshot 2B** — `xxd` of a raw tree object (first 20 lines):

![Screenshot 2B](2.b.jpeg)

### Phase 3: Staging Area

**Screenshot 3A** — `pes init` → `pes add` → `pes status` sequence:

![Screenshot 3A](3.a.jpeg)

**Screenshot 3B** — `cat .pes/index` showing the text-format index:

![Screenshot 3B](3.b.jpeg)

### Phase 4: Commits and History

**Screenshot 4A** — `pes log` output with three commits:

![Screenshot 4A](4.a.jpeg)

**Screenshot 4B** — `find .pes -type f | sort` showing object store growth:

![Screenshot 4B](4.b.jpeg)

**Screenshot 4C** — `cat .pes/refs/heads/main` and `cat .pes/HEAD` showing the reference chain:

![Screenshot 4C](4.c.jpeg)

### Integration Test

**Final** — Full integration test (`make test-integration`):

![Integration Test Part 1](integration1.jpeg)

![Integration Test Part 2](integration2.jpeg)

