# Run from project root, e.g. protobuf/protobuf-doc.sh
# NOTE: protoc-gen-doc must be on the path
# NOTE: The output is ../nanoapi.github.com/protobuf - clone nanoapi.github.com as a sibling to this repo or adjust paths.

mkdir -p doc
protoc  --plugin=protoc-gen-doc=`which protoc-gen-doc` --proto_path=protobuf --doc_opt=html,index.html --doc_out=../nanoapi.github.io/protobuf core.proto
