# ---- build stage -------------------------------------------------------------
FROM maven:3.9-eclipse-temurin-17 AS build
WORKDIR /build
COPY pom.xml .
RUN mvn -q -B dependency:go-offline          # cached layer: only re-runs when pom.xml changes
COPY src ./src
RUN mvn -q -B -DskipTests package

# ---- runtime stage -----------------------------------------------------------
FROM eclipse-temurin:17-jre
WORKDIR /app
COPY --from=build /build/target/control-plane-1.0.0.jar app.jar
EXPOSE 8081
ENTRYPOINT ["java", "-XX:MaxRAMPercentage=75", "-jar", "app.jar"]
