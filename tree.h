#include <jansson.h>

struct TreeNode {
  char* name;
  char* host_path;
  struct TreeNode* parent;
  struct TreeNode** children;
  int num_children;
};

struct TreeNode* createNode(const char* name, struct TreeNode* parent, const char* host_path);
void addChild(struct TreeNode* parent, struct TreeNode* child);
struct TreeNode* getNodeByPath(struct TreeNode* root, const char* path);
const char* getHostPath(struct TreeNode* root, const char* path);
void printTree(struct TreeNode* node, int level);
void readTreeFromJsonRecursive(struct TreeNode* parent, json_t* children);
struct TreeNode* readTreeFromJsonFile(const char* filename);
struct TreeNode* readTreeFromJson(json_t* root);
