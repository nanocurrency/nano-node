import argparse
import copy

try:
    from github import Github
    from mdutils import MdUtils
except BaseException:
    exit("Error: run 'pip install PyGithub mdutils'")

SECTIONS = {
    "Major Changes": [
        "major",
    ],
    "Protocol Changes": [
        "protocol change",
    ],
    "Node Configuration Updates": [
        "toml",
        "configuration default change",
    ],
    "RPC Updates": [
        "rpc",
    ],
    "CLI Updates": [
        "cli",
    ],
    "IPC Updates": [
        "ipc",
    ],
    "Deprecation/Removal": [
        "deprecation",
        "removal",
    ],
    "Developer Wallet": [
        "qt wallet",
    ],
    "Developer/Debug Options": [
        "debug",
    ],
    "Fixed Bugs": [
        "bug",
    ],
    "Implemented Enhancements": [
        "enhancement",
        "functionality quality improvements",
        "non-functional change",
        "performance",
        "quality improvements",
    ],
    "Build, Test, Automation, & Chores": [
        "build-error",
        "documentation",
        "routine",
        "sanitizers",
        "static-analysis",
        "tool",
        "unit test",
        "universe",
    ],
    "Other": []
}


class cliArgs():
    def __init__(self):
        parse = argparse.ArgumentParser(
            prog="changelog",
            description="Generate Changelogs between tags or commits"
        )
        parse.add_argument(
            '-r', '--repo',
            help="<org/repo> to generate logs for",
            type=str, action="store",
            default='nanocurrency/nano-node',
        )
        parse.add_argument(
            '-s', '--start',
            help="Starting reference for Changelog",
            type=str, action="store",
            required=True,
        )
        parse.add_argument(
            '-e', '--end',
            help="Ending reference for Changelog",
            type=str, action="store",
            required=True,
        )
        parse.add_argument(
            '--pat',
            help="Personal Access Token",
            type=str, action="store",
            required=True,
        )
        options = parse.parse_args()
        self.repo = options.repo
        self.start = options.start
        self.end = options.end
        self.pat = options.pat

    def __repr__(self):
        return "<cliArgs(repo='{0}', start='{1}', end='{2}', pat='{3}')>" \
            .format(self.repo, self.start, self.end, self.pat)

    def __str__(self):
        return "Generating a changelog for {0} starting with {1} " \
            "and ending with {2}".format(self.repo, self.start, self.end)


class changelogRepo:
    def __init__(self, args):
        github = Github(args.pat)
        self.repo = github.get_repo(args.repo)
        self.start = args.start
        self.end = args.end
        try:
            self.startCommit = self.repo.get_commit(args.start)
        except BaseException:
            exit("Error finding commit for " + args.start)
        try:
            self.endCommit = self.repo.get_commit(args.end)
        except BaseException:
            exit("Error finding commit for " + args.end)
        self.tree = self.repo.compare(self.start, self.end).commits
        self.commits = {}
        for commit in self.tree:
            for pull in commit.get_pulls():
                labels = []
                for label in pull.labels:
                    labels.append(label.name)
                self.commits[pull.number] = {
                    "Title": pull.title,
                    "Url": pull.html_url,
                    "labels": labels
                }

    def __repr__(self):
        return "<changelogRepo(repo='{0}', start='{1}', startCommit='{2}', " \
            "end='{3}', endCommit='{4}', tree='{5}', commits='{6}".format(
                self.repo, self.start, self.startCommit, self.end,
                self.endCommit, self.tree, self.commits,
            )


class generateMarkdown():
    def __repr__(self):
        return "<generateMarkdown(mdFile={0})>".format(
            self.mdFile
        )

    def __init__(self, repo):
        self.mdFile = MdUtils(
            file_name='CHANGELOG', title='CHANGELOG'
        )
        self.mdFile.new_line(
            "## **Release** " + \
            "[{0}](https://github.com/nanocurrency/nano-node/tree/{0})"\
                .format(repo.end))
        sort = self.pull_to_section(repo.commits)

        for section, prs in sort.items():
            self.write_header(section)
            for pr in prs:
                self.write_PR(pr, repo.commits[pr[0]])
        self.mdFile.create_md_file()

    def write_header(self, section):
        self.mdFile.new_header(level=3, title=section)
        self.mdFile.new_line("---")
        self.mdFile.new_line(
            "|Pull Request|Title (*indicates breaking or configuration \
                default change)")
        self.mdFile.new_line("|:-:|:--")

    def write_PR(self, pr, info):
        imp = ""
        if pr[1]:
            imp = "* "
        self.mdFile.new_line(
            "|[#{0}]({1})|{2}{3}".format(
                pr[0], info['Url'], imp, info['Title']))

    def handle_labels(self, labels):
        for section, values in SECTIONS.items():
            for label in labels:
                if label in values:
                    if any(
                        string in labels for string in [
                            'breaking',
                            'configuration change default']):
                        return section, True
                    else:
                        return section, False
        return 'Other', False

    def pull_to_section(self, commits):
        sect = copy.deepcopy(SECTIONS)
        result = {}
        for a in sect:
            sect[a] = []
        for pull, info in commits.items():
            section, important = self.handle_labels(info['labels'])
            sect[section].append([pull, important])
        for a in sect:
            if len(sect[a]) > 0:
                result[a] = sect[a]
        return result


if __name__ == "__main__":
    args = cliArgs()
    repo = changelogRepo(args)
    generateMarkdown(repo)
