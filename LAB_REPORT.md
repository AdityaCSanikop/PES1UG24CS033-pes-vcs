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

---

## Phase 5 & 6: Analysis Questions

### Q5.1: Implementing `pes checkout <branch>`

To implement `pes checkout <branch>`, the following changes are needed:

**Files that change in `.pes/`:**
- `.pes/HEAD` is updated to contain `ref: refs/heads/<branch>`, pointing to the new branch.

**Working directory changes:**
1. Read the commit hash from `.pes/refs/heads/<branch>`.
2. Read the commit object to get its root tree hash.
3. Recursively walk the tree object, reading each blob, and write the file contents to the working directory at the correct paths.
4. Update `.pes/index` to reflect the checked-out tree so that `pes status` shows a clean state.

**What makes this complex:**
- Files present in the current branch but absent in the target branch must be deleted from the working directory.
- Files present in the target branch but absent in the current branch must be created.
- If the user has uncommitted modifications to tracked files that also differ between branches, checkout must abort to avoid data loss. Detecting this requires comparing the working directory state against both the current index and the target tree.
- Directory creation and removal adds further edge cases (e.g., removing the last file in a directory should remove the directory).

### Q5.2: Detecting Dirty Working Directory Conflicts

To detect whether checkout is safe, compare three versions of each tracked file using only the index and object store:

1. **Index vs. HEAD tree:** For each file in the index, compare its stored blob hash against the blob hash in the current HEAD commit's tree. If they differ, the file has staged changes that would be lost.

2. **Working directory vs. Index:** For each file in the index, `stat()` the working file and compare `mtime` and `size` against the index entry. If they differ, the file has unstaged modifications. To be certain, re-read and re-hash the file contents and compare against the index blob hash.

3. **Target tree vs. HEAD tree:** For each file that differs between the current HEAD tree and the target branch's tree, check if that file is dirty (from steps 1 or 2). If a file is both dirty and differs between branches, checkout must refuse — otherwise the user's modifications would be silently overwritten.

Files that are identical between both branches can be safely ignored even if dirty, since checkout would not change them. Files that are untracked (not in the index) are also safe unless the target tree introduces a file with the same name, which would cause a conflict.

### Q5.3: Detached HEAD and Recovering Commits

In detached HEAD state, `.pes/HEAD` contains a raw commit hash (e.g., `a1b2c3d4...`) instead of a symbolic reference (e.g., `ref: refs/heads/main`).

**What happens when you commit in this state:**
- New commits are created normally. Each new commit's parent points to the previous commit.
- However, `head_update` writes the new commit hash directly into `.pes/HEAD` (since there is no branch ref to update).
- No branch file in `.pes/refs/heads/` is updated, so no branch tracks these commits.

**The danger:**
- If the user checks out a branch (e.g., `pes checkout main`), HEAD is overwritten to point to that branch. The commits made in detached HEAD state are now **orphaned** — no reference points to them. They still exist in the object store but are unreachable by walking any branch.

**Recovery:**
- If the user remembers (or recorded) the commit hash, they can directly check it out or create a branch pointing to it: `git branch recovery-branch <hash>`.
- Git provides `git reflog`, which logs every change to HEAD, allowing users to find the orphaned commit hash. PES-VCS does not have a reflog, so without the hash these commits would eventually be lost to garbage collection.

