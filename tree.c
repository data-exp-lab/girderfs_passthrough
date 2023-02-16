#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

#include "tree.h"


struct TreeNode* createNode(const char* name, struct TreeNode* parent, const char* host_path) {
  struct TreeNode* node = (struct TreeNode*) malloc(sizeof(struct TreeNode));
  node->name = (char*) malloc(strlen(name) + 1);
  strcpy(node->name, name);
  if (host_path) {
    node->host_path = (char*) malloc(strlen(host_path) + 1);
    strcpy(node->host_path, host_path);
  } else {
    node->host_path = NULL;
  }
  node->parent = parent;
  node->children = NULL;
  node->num_children = 0;
  return node;
}

void addChild(struct TreeNode* parent, struct TreeNode* child) {
  parent->num_children++;
  parent->children = (struct TreeNode**) realloc(parent->children, sizeof(struct TreeNode*) * parent->num_children);
  parent->children[parent->num_children - 1] = child;
}

void printTree(struct TreeNode* node, int level) {
  for (int i = 0; i < level; i++) {
    printf("\t");
  }
  printf("%s -> %s\n", node->name, node->host_path);
  for (int i = 0; i < node->num_children; i++) {
    printTree(node->children[i], level + 1);
  }
}

void readTreeFromJsonRecursive(struct TreeNode* parent, json_t* children) {
  for (int i = 0; i < json_array_size(children); i++) {
    json_t* child = json_array_get(children, i);
    if (!json_is_object(child)) {
      fprintf(stderr, "error: child is not an object\n");
      continue;
    }
    json_t* name = json_object_get(child, "name");
    if (!json_is_string(name)) {
      fprintf(stderr, "error: name is not a string\n");
      continue;
    }

    json_t* host_path = json_object_get(child, "host_path");
    struct TreeNode* childNode;
    if (!json_is_string(host_path)) {
      childNode = createNode(json_string_value(name), parent, NULL);
    } else {
      childNode = createNode(json_string_value(name), parent, json_string_value(host_path));
    }
    addChild(parent, childNode);

    json_t* childChildren = json_object_get(child, "children");
    if (json_is_array(childChildren)) {
      readTreeFromJsonRecursive(childNode, childChildren);
    }
  }
}

struct TreeNode* readTreeFromJsonFile(const char* filename) {
  json_error_t error;
  json_t* root = json_load_file(filename, 0, &error);
  if (!root) {
    fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
    return NULL;
  }
  if (!json_is_object(root)) {
    fprintf(stderr, "error: root is not an object\n");
    json_decref(root);
    return NULL;
  }
  return readTreeFromJson(root);
}

struct TreeNode* readTreeFromJson(json_t* root) {
  if (!json_is_object(root)) {
    fprintf(stderr, "error: root is not an object\n");
    json_decref(root);
    return NULL;
  }

  struct TreeNode* node = createNode("root", NULL, NULL);
  json_t* children = json_object_get(root, "children");
  if (!json_is_array(children)) {
    fprintf(stderr, "error: children is not an array\n");
    json_decref(root);
    return node;
  }

  for (int i = 0; i < json_array_size(children); i++) {
    json_t* child = json_array_get(children, i);
    if (!json_is_object(child)) {
      fprintf(stderr, "error: child is not an object\n");
      continue;
    }
    json_t* name = json_object_get(child, "name");
    if (!json_is_string(name)) {
      fprintf(stderr, "error: name is not a string\n");
      continue;
    }
    struct TreeNode* childNode;
    json_t* host_path = json_object_get(child, "host_path");
    if (!json_is_string(host_path)) {
      childNode = createNode(json_string_value(name), node, NULL);
    } else {
      childNode = createNode(json_string_value(name), node, json_string_value(host_path));
    }
    addChild(node, childNode);

    json_t* childChildren = json_object_get(child, "children");
    if (json_is_array(childChildren)) {
      readTreeFromJsonRecursive(childNode, childChildren);
    }
  }

  json_decref(root);
  return node;
}

struct TreeNode* getNodeByPath(struct TreeNode* root, const char* path) {
  char buffer[1024];
  strcpy(buffer, path);

  char* token = strtok(buffer, "/");
  struct TreeNode* node = root;
  while (token != NULL) {
    int found = 0;
    for (int i = 0; i < node->num_children; i++) {
      if (strcmp(node->children[i]->name, token) == 0) {
        node = node->children[i];
        found = 1;
        break;
      }
    }
    if (!found) {
      return NULL;
    }
    token = strtok(NULL, "/");
  }
  return node;
}

const char* getHostPath(struct TreeNode* root, const char* path) {
  struct TreeNode* node = getNodeByPath(root, path);
  if (node) {
    return node->host_path;
  } else {
    return "/tmp";
  }
}

int maiz(int argc, char* argv[]) {
  if (argc < 2) {
    printf("Usage: %s <filename>\n", argv[0]);
    return 1;
  }

  struct TreeNode* root = readTreeFromJsonFile(argv[1]);
  if (!root) {
    printf("Error reading file %s\n", argv[1]);
    return 1;
  }

  // Other code to use the tree structure...
  printTree(root, 0);
  printf("foo = %s", getHostPath(root, "/home/user1/downloads/file.txt"));

  return 0;
}
