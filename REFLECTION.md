Where the Agent was Most Reliable
The agent was most reliable when debugging. When debugging make memcheck, memcheck crashed. This crash pointed towards an issue in rbtree.c. However, the agent had correctly identified that the issue was not in rbtree.c. In reality, the issue was a stale address sanitizer that was being reused by Makefile. As a result, the agent correctly fixed the root cause in make memecheck. This proved the agent’s reliability. Without the agent, I most likely would have spent a lot of time chasing a bug in rbtree.c that does not exist.  

Where the Agent was Least Reliable
The agent was least reliable when it came to the git environment. The agent struggled with a GCC version mismatch. However, the agent did not flag this as an issue until later. During debugging and when asked for potential risks, this was never brought up. The issue was only addressed late in Milestone 3. 

A Bug Introduced and Caught
The agent was extremely reliable in this category, and no major bugs were introduced. However, during planning, the agent would develop a bug and backtrack to fix it before finishing the plan. During the planning of the table-driven deletion tests, the agent created tests that were redundant and incorrect. These tests would return true whether the code was working or not. So, these tests served no actual testing purpose. While drafting, the agent noticed this and redrafted the tests. This was very important, and I was impressed that the agent was able to catch bugs in its own planning process and correct them before I intervened.

What Surprised me about C
Using C instead of Java surprised me with the additional complexities. C provides much less help or hand-holding than Java does. One of the things that surprised me was memory management. Specifically, having to think about and design programs to handle and prevent memory leaks. This surprised me as I was used to Java’s garbage collector.