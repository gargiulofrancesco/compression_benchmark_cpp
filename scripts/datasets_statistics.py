import os
import json

DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data")

def get_common_prefix_len(s1, s2):
    """Calculates the length of the common prefix between two strings."""
    min_len = min(len(s1), len(s2))
    for i in range(min_len):
        if s1[i] != s2[i]:
            return i
    return min_len

def analyze_file(filepath):
    filename = os.path.basename(filepath)
    print(f"Analyzing {filename}...")
    
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            data = json.load(f)
    except Exception as e:
        print(f"  ❌ Error reading {filename}: {e}")
        return

    if not data or not isinstance(data, list):
        print(f"  ❌ Error: {filename} is not a valid list of strings.")
        return

    # Convert all strings to bytes for accurate byte-level analysis
    data = [s.encode('utf-8') for s in data]
    n = len(data)

    # 1. Uniqueness / Cardinality
    unique_count = len(set(data))
    unique_pct = (unique_count / n) * 100
    
    # 2. Average Length
    total_len = sum(len(s) for s in data)
    avg_len = total_len / n
    
    # 3. Alphabet Size
    vocab = set()
    for s in data:
        vocab.update(s)
    sigma = len(vocab)

    # 4. Average LCP
    data.sort()
    lcp_sum = 0
    if n > 1:
        for i in range(1, n):
            lcp_sum += get_common_prefix_len(data[i-1], data[i])
        avg_lcp = lcp_sum / (n - 1)
    else:
        avg_lcp = 0

    # Human Readable Output
    print(f"  ----------------------------------------")
    print(f"  File:           {filename}")
    print(f"  Count (N):      {n:,}")
    print(f"  Unique (%):     {unique_pct:.2f}%")  
    print(f"  Avg Length:     {avg_len:.2f} bytes")
    print(f"  Alphabet Size:  {sigma}")
    print(f"  Avg LCP:        {avg_lcp:.2f} bytes")
    print(f"  ----------------------------------------\n")

def main():
    if not os.path.exists(DATA_DIR):
        print(f"Directory {DATA_DIR} not found.")
        return

    files = sorted([f for f in os.listdir(DATA_DIR) if f.endswith('.json')])
    
    if not files:
        print(f"No .json files found in {DATA_DIR}")
        return

    print(f"Found {len(files)} datasets. Starting full analysis...\n")

    for f in files:
        analyze_file(os.path.join(DATA_DIR, f))

if __name__ == "__main__":
    main()