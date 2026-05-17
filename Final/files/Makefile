CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET   = disasterguard
SRC      = main.cpp

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

# Ejecutar todos los casos de prueba
test: $(TARGET)
	@echo "=== Case 1: Alerta sísmica básica ==="
	@./$(TARGET) tests/case1.txt
	@echo ""
	@echo "=== Case 2: Encadenamiento de reglas ==="
	@./$(TARGET) tests/case2.txt
	@echo ""
	@echo "=== Case 3: Sin activación ==="
	@./$(TARGET) tests/case3.txt
	@echo ""
	@echo "=== Case 4: Conflicto de acciones ==="
	@./$(TARGET) tests/case4.txt
	@echo ""
	@echo "=== Case 5: Reglas redundantes ==="
	@./$(TARGET) tests/case5.txt
	@echo ""
	@echo "=== Case 6: Escenario completo de desastres ==="
	@./$(TARGET) tests/disaster.txt

clean:
	rm -f $(TARGET)
