
pc:
	protoc --go_out=. --go_opt=paths=source_relative --go-grpc_out=. --go-grpc_opt=paths=source_relative routeguide/route_guide.proto

rs:
	go run server/server.go -json_db_file testdata/route_guide_db.json &

rc:
	go run client/client.go
